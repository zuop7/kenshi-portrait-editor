#define NOMINMAX
#include <windows.h>
#include <cmath>
#include <float.h>
#include <cctype>
#include <stdexcept>
#include <iomanip>
#include <type_traits>
#include <kenshi/Kenshi.h>
#include <mygui/MyGUI_IUnlinkWidget.h>
#include <mygui/MyGUI_WidgetManager.h>
#include <map>
#include <sstream>
#include <vector>
#include <algorithm>
#include <ogre/OgreImage.h>
#include <ogre/OgreTextureManager.h>
#include <Debug.h>
#include <core/Functions.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Character.h>
#include <kenshi/Platoon.h>
#include <kenshi/Appearance.h>
#include <kenshi/InstanceID.h>
#include <kenshi/GameData.h>
#include <kenshi/RaceData.h>
#include <kenshi/RenderToTextureotron.h>
#include <kenshi/gui/MainBarGUI.h>
#include <kenshi/gui/ContextMenu.h>
#define ContextMenu ContextMenuDuplicate
#include <kenshi/PlayerInterface.h>
#undef ContextMenu
#include <kenshi/gui/PortraitManager.h>
#include <kenshi/gui/SquadManagementScreen.h>
#include <mygui/MyGUI.h>
#include <ogre/Ogre.h>
#include <ogre/OgreHardwarePixelBuffer.h>
static_assert(std::is_same<decltype(&PortraitManager::setImageWidget),void (PortraitManager::*)(const hand&,MyGUI::ImageBox*,bool)>::value,"Portrait assignment ABI changed");
static_assert(std::is_same<decltype(&RenderToTextureotron::renderCharacter),void (RenderToTextureotron::*)(Character*,Ogre::SharedPtr<Ogre::Texture>,const iVector2&,const Ogre::TRect<float>&)>::value,"Portrait renderer ABI changed");
static_assert(std::is_same<decltype(&SquadManagementScreen::notifyEndDropPortrait),void (SquadManagementScreen::*)(wraps::BaseLayout*,wraps::DDItemInfo,bool)>::value,"Portrait drop ABI changed");
static_assert(std::is_same<decltype(&MainBarGUI::portraitNotifyItem),void (MainBarGUI::*)(MyGUI::ItemBox*,const MyGUI::IBNotifyItemData&)>::value,"Portrait click ABI changed");
static_assert(std::is_same<decltype(&PortraitMainCellView::update),void (PortraitMainCellView::*)(const MyGUI::IBDrawItemInfo&,PortraitData*)>::value,"Portrait cell ABI changed");
static_assert(std::is_same<decltype(&MainBarGUI::_NV_update),void (MainBarGUI::*)()>::value,"HUD update ABI changed");
static_assert(std::is_same<decltype(&Character::_NV_setHandle),void (Character::*)(const hand&)>::value,"Character handle ABI changed");
static_assert(std::is_same<decltype(&ActivePlatoon::_NV_addActiveObject),bool (ActivePlatoon::*)(RootObject*)>::value,"Squad insertion ABI changed");
static_assert(std::is_same<decltype(&ActivePlatoon::addCharacterAt),void (ActivePlatoon::*)(RootObject*,int)>::value,"Indexed squad insertion ABI changed");
namespace {
bool pluginReady=false,failed=false;
DWORD identityThread=0;
struct Settings {float yaw,pitch,height,horizontal,zoom,r,g,b,brightness,opacity;int background;Settings():yaw(0),pitch(0),height(0),horizontal(0),zoom(1),r(.25f),g(.25f),b(.25f),brightness(1),opacity(1),background(0){} };
float Bounded(float value,float low,float high,float fallback){return _finite(value)?std::max(low,std::min(high,value)):fallback;}
Settings Sanitize(Settings s,size_t backgroundCount){
 s.pitch=Bounded(s.pitch,-35,35,0);s.yaw=Bounded(s.yaw,-180,180,0);s.height=Bounded(s.height,-1,1,0);s.horizontal=Bounded(s.horizontal,-1,1,0);s.zoom=Bounded(s.zoom,.6f,1.8f,1);
 s.r=Bounded(s.r,0,1,.25f);s.g=Bounded(s.g,0,1,.25f);s.b=Bounded(s.b,0,1,.25f);s.brightness=Bounded(s.brightness,0,2,1);s.opacity=Bounded(s.opacity,0,1,1);
 if(s.background<0 || size_t(s.background)>=backgroundCount)s.background=0;return s;
}
std::map<std::string,Settings> settings;
std::map<std::string,Ogre::TexturePtr> composed;
std::map<std::string,DWORD> requested;
unsigned int textureSerial=0;
std::map<std::string,hand> pendingComposition;
std::map<std::string,hand> pendingSquadRefresh;
void ComposeRegistered(Character* c);
std::string Key(Character* c);
std::map<std::string,Ogre::TRect<float> > nativeUV;
std::map<std::string,std::string> nativeTextures;
struct Capture {hand character;MyGUI::ImageBox* widget;};
__declspec(thread) Capture* capturing=0;
void (*originalTexture)(MyGUI::ImageBox*,const std::string&)=0;
void Texture(MyGUI::ImageBox* image,const std::string& name){
 if(!image)return;originalTexture(image,name);
 if(!pluginReady || failed || !capturing || capturing->widget!=image)return;
 try{Character* c=capturing->character.getCharacter();if(c && name.find("PortraitEditorComposed_")!=0)nativeTextures[Key(c)]=name;}
 catch(const std::exception& e){ErrorLog(std::string("Portrait Editor texture capture: ")+e.what());}
}

void (*originalUV)(MyGUI::ImageBox*,const MyGUI::FloatRect&)=0;
void UV(MyGUI::ImageBox* image,const MyGUI::FloatRect& rect){
 if(!image)return;originalUV(image,rect);
 if(!pluginReady || failed || !capturing || capturing->widget!=image)return;
 if(!_finite(rect.left) || !_finite(rect.top) || !_finite(rect.right) || !_finite(rect.bottom) || rect.left<0 || rect.top<0 || rect.right>1 || rect.bottom>1 || rect.right<=rect.left || rect.bottom<=rect.top)return;
 try{Character* c=capturing->character.getCharacter();if(c)nativeUV[Key(c)]=Ogre::TRect<float>(rect.left,rect.top,rect.right,rect.bottom);}
 catch(const std::exception& e){ErrorLog(std::string("Portrait Editor UV capture: ")+e.what());}
}

void (*originalSetImage)(PortraitManager*,const hand&,MyGUI::ImageBox*,bool)=0;
void SetImage(PortraitManager* pm,const hand& character,MyGUI::ImageBox* image,bool force){
 if(!pm || !image)return;
 if(!pluginReady || failed){originalSetImage(pm,character,image,force);return;}
 Capture capture;capture.character=character;capture.widget=image;Capture* previous=capturing;capturing=&capture;
 try{originalSetImage(pm,character,image,force);}catch(...){capturing=previous;throw;}capturing=previous;
}
std::string resources,config,backgroundFolder;
std::vector<std::string> customBackgrounds;
std::map<std::string,std::vector<unsigned char> > backgroundPixels;
char LowerExtension(char c){return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));}
void ScanBackgrounds(){
 WIN32_FIND_DATAA item;HANDLE search=FindFirstFileA((backgroundFolder+"/*").c_str(),&item);if(search==INVALID_HANDLE_VALUE)return;
 struct FindScope{HANDLE value;FindScope(HANDLE h):value(h){}~FindScope(){FindClose(value);}} findScope(search);
 do{if(item.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue;std::string name=item.cFileName;size_t dot=name.find_last_of(".");if(dot==std::string::npos)continue;std::string ext=name.substr(dot);std::transform(ext.begin(),ext.end(),ext.begin(),LowerExtension);if(customBackgrounds.size()<128 && (ext==".png" || ext==".jpg" || ext==".jpeg"))customBackgrounds.push_back(name);}while(FindNextFileA(search,&item));std::sort(customBackgrounds.begin(),customBackgrounds.end());
}
size_t BackgroundCount(){return 6+customBackgrounds.size();}
MyGUI::Widget *window=0,*popup=0;
MyGUI::ImageBox *preview=0,*backdrop=0;
MyGUI::TextBox *title=0,*bgLabel=0,*status=0;
MyGUI::ScrollBar *rotation=0,*tilt=0,*height=0,*horizontal=0,*zoom=0,*red=0,*green=0,*blue=0,*brightness=0,*opacity=0;
MyGUI::TextBox *redLabel=0,*greenLabel=0,*blueLabel=0,*brightnessLabel=0,*opacityLabel=0;
MyGUI::Button *resetButton=0,*cancelButton=0,*applyButton=0;
hand editing,pending;
bool portraitMenuRequested=false,extensionShown=false,nativeMenuSeen=false;
MyGUI::IntPoint portraitMenuPosition;
DWORD portraitMenuRequestedAt=0;
bool portraitHoldEligible=false,portraitHoldComplete=false,portraitHoldReleasedEarly=false;
MyGUI::Button* popupAction=0;
Settings draft;
bool opened=false,updating=false,dirty=false,resourcesReady=false;
typedef void (*ButtonAction)(MyGUI::Widget*);
std::map<MyGUI::Widget*,ButtonAction> buttonActions;
std::map<MyGUI::ScrollBar*,MyGUI::EditBox*> numberFields;
struct WidgetLifetime : MyGUI::IUnlinkWidget {
 void _unlinkWidget(MyGUI::Widget* widget){
  buttonActions.erase(widget);
  for(std::map<MyGUI::ScrollBar*,MyGUI::EditBox*>::iterator i=numberFields.begin();i!=numberFields.end();){
   if(i->first==widget || i->second==widget){numberFields.erase(i++);opened=false;}else ++i;
  }
  if(widget==window){window=0;opened=false;editing.setNull();title=0;bgLabel=0;status=0;rotation=0;tilt=0;height=0;horizontal=0;zoom=0;red=0;green=0;blue=0;brightness=0;opacity=0;redLabel=0;greenLabel=0;blueLabel=0;brightnessLabel=0;opacityLabel=0;resetButton=0;cancelButton=0;applyButton=0;}
  if(widget==popup){popup=0;popupAction=0;portraitMenuRequested=false;extensionShown=false;}
  if(widget==preview){preview=0;opened=false;}
  if(widget==backdrop)backdrop=0;
  if(widget==popupAction){popupAction=0;}
  if(widget==title){title=0;opened=false;}
  if(widget==bgLabel){bgLabel=0;opened=false;}
  if(widget==status){status=0;opened=false;}
  if(widget==tilt){tilt=0;opened=false;}
  if(widget==rotation){rotation=0;opened=false;}
  if(widget==height){height=0;opened=false;}
  if(widget==horizontal){horizontal=0;opened=false;}
  if(widget==zoom){zoom=0;opened=false;}
  if(widget==red){red=0;opened=false;}
  if(widget==green){green=0;opened=false;}
  if(widget==blue){blue=0;opened=false;}
  if(widget==brightness){brightness=0;opened=false;}
  if(widget==opacity){opacity=0;opened=false;}
  if(widget==redLabel){redLabel=0;opened=false;}
  if(widget==greenLabel){greenLabel=0;opened=false;}
  if(widget==blueLabel){blueLabel=0;opened=false;}
  if(widget==brightnessLabel){brightnessLabel=0;opened=false;}
  if(widget==opacityLabel){opacityLabel=0;opened=false;}
  if(widget==resetButton){resetButton=0;opened=false;}
  if(widget==cancelButton){cancelButton=0;opened=false;}
  if(widget==applyButton){applyButton=0;opened=false;}

 }
} widgetLifetime;
bool lifetimeRegistered=false;
void Resources(){if(!resourcesReady){Ogre::ResourceGroupManager::getSingleton().addResourceLocation(resources,"FileSystem","General");Ogre::ResourceGroupManager::getSingleton().addResourceLocation(backgroundFolder,"FileSystem","General");resourcesReady=true;}}
bool EditorControlsReady(){return numberFields.size()==10 && window && preview && backdrop && title && bgLabel && status && rotation && tilt && height && horizontal && zoom && red && green && blue && brightness && opacity && redLabel && greenLabel && blueLabel && brightnessLabel && opacityLabel && resetButton && cancelButton && applyButton;}
void Create();
void SyncNumbers();
void LayoutNumbers();
void CommitFocusedNumber();
void AcceptNumber(MyGUI::EditBox* sender);
void BlurNumber(MyGUI::Widget* sender,MyGUI::Widget* next);
void EnsureUI(){
 MyGUI::Gui* gui=MyGUI::Gui::getInstancePtr();if(!gui || !MyGUI::RenderManager::getInstancePtr())return;
 if(EditorControlsReady() && popup && popupAction)return;
 if(window)gui->destroyWidget(window);if(popup)gui->destroyWidget(popup);
 Create();
}
DWORD changedAt=0;
const char* bgNames[]={"Default","Teal","Black","Sand","Custom colour","Bordeaux"};
const char* bgFiles[]={"","portrait_teal.png","portrait_black.png","portrait_sand.png","portrait_custom.png","portrait_bordeaux.png"};
std::string Key(Character* c) {
 if(!c)return "";InstanceID* i=c->getInstanceID();if(i && !i->uid.empty())return i->uid;
 const hand& h=c->getHandle();std::ostringstream key;key<<"character_"<<h.container<<'_'<<h.containerSerial<<'_'<<h.index<<'_'<<h.serial;
 return key.str();
}
Settings Get(Character* c) {
 std::string k=Key(c);if(k.empty())return Settings();
 if(settings.count(k))return settings[k];
 char value[256];GetPrivateProfileStringA(k.c_str(),"view","0 0 1 0",value,256,config.c_str());
 Settings s;std::istringstream in(value);in>>s.yaw>>s.height>>s.zoom>>s.background;in>>s.r>>s.g>>s.b;in>>s.brightness>>s.opacity;in>>s.horizontal;in>>s.pitch;s.horizontal=std::max(-1.f,std::min(1.f,s.horizontal));s.brightness=std::max(0.f,std::min(2.f,s.brightness));s.opacity=std::max(0.f,std::min(1.f,s.opacity));
 s.yaw=std::max(-180.f,std::min(180.f,s.yaw));s.height=std::max(-1.f,std::min(1.f,s.height));s.zoom=std::max(.6f,std::min(1.8f,s.zoom));s.background=std::max(0,std::min(5,s.background));
 if(!_finite(s.yaw) || !_finite(s.height) || !_finite(s.horizontal) || !_finite(s.zoom) || !_finite(s.r) || !_finite(s.g) || !_finite(s.b) || !_finite(s.brightness) || !_finite(s.opacity))s=Settings();
 s.r=std::max(0.f,std::min(1.f,s.r));s.g=std::max(0.f,std::min(1.f,s.g));s.b=std::max(0.f,std::min(1.f,s.b));
 char file[260];GetPrivateProfileStringA(k.c_str(),"background_file","",file,260,config.c_str());
 if(file[0]){s.background=0;for(size_t i=0;i<customBackgrounds.size();++i)if(customBackgrounds[i]==file){s.background=int(i)+6;break;}}
 s=Sanitize(s,BackgroundCount());settings[k]=s;return s;
}
Settings Effective(Character* c){return opened && editing.getCharacter()==c?draft:Get(c);}
bool Store(Character* c,const Settings& value){
 std::string k=Key(c);if(k.empty() || config.empty())return false;
 Settings s=Sanitize(value,BackgroundCount());settings[k]=s;
 std::ostringstream out;out<<s.yaw<<' '<<s.height<<' '<<s.zoom<<' '<<s.background<<' '<<s.r<<' '<<s.g<<' '<<s.b<<' '<<s.brightness<<' '<<s.opacity<<' '<<s.horizontal<<' '<<s.pitch;
 BOOL viewWritten=WritePrivateProfileStringA(k.c_str(),"view",out.str().c_str(),config.c_str());
 BOOL backgroundWritten=WritePrivateProfileStringA(k.c_str(),"background_file",s.background>=6?customBackgrounds[s.background-6].c_str():"",config.c_str());
 if(!viewWritten || !backgroundWritten){ErrorLog("Portrait Editor: settings could not be written to portrait_editor.ini");return false;}
 return true;
}
// Automatic squad moves (capture, prison release, recruitment) do not go
// through the portrait drag callback. The insertion functions clear the old
// handle and assign the final one AFTER _setPlatoon returns. Keep the snapshot
// on this native call stack only, never in a pointer-keyed cross-frame cache.
struct IdentityTransfer {
 Character* address;Settings saved;std::string key;bool captured,wasEditing,wasPending;
 IdentityTransfer():address(0),captured(false),wasEditing(false),wasPending(false){}
};
__declspec(thread) IdentityTransfer* identityTransfer=0;
struct IdentityScope {
 IdentityTransfer* previous;
 IdentityScope(IdentityTransfer& value):previous(identityTransfer){if(value.captured)identityTransfer=&value;}
 ~IdentityScope(){identityTransfer=previous;}
};
void CaptureIdentity(Character* c,IdentityTransfer& snapshot){
 if(!pluginReady || failed || !c || config.empty() || GetCurrentThreadId()!=identityThread)return;
 // The outer insertion owns the snapshot through its temporary null handle.
 if(identityTransfer && identityTransfer->address==c)return;
 const hand& h=c->getHandle();if(h.isNull())return;
 std::string k=Key(c);if(k.empty())return;
 // Most insertions belong to uncustomized NPCs. Do not read their INI rows.
 if(!settings.count(k) && !c->isPlayerCharacter())return;
 char stored[256]={0};GetPrivateProfileStringA(k.c_str(),"view","",stored,sizeof(stored),config.c_str());
 if(!stored[0])return;
 snapshot.saved=Get(c);snapshot.key=k;snapshot.address=c;
 snapshot.wasEditing=editing.getCharacter()==c;snapshot.wasPending=pending.getCharacter()==c;
 snapshot.captured=true;
}
void RestoreIdentity(Character* c,const IdentityTransfer& snapshot){
 if(!snapshot.captured || !c || c!=snapshot.address)return;
 hand current=c->getHandle();if(current.isNull() || current.getCharacter()!=c)return;
 std::string k=Key(c);if(k.empty() || k==snapshot.key)return;
 bool written=Store(c,snapshot.saved);
 nativeUV.erase(k);nativeTextures.erase(k);composed.erase(k);requested.erase(k);
 pendingComposition.erase(snapshot.key);pendingSquadRefresh.erase(snapshot.key);
 if(snapshot.wasEditing)editing=current;if(snapshot.wasPending)pending=current;
 // Render only later, from the existing bounded HUD refresh queue. No GUI or
 // portrait manager calls run inside engine membership/handle mutation.
 pendingSquadRefresh[k]=current;
 DebugLog(std::string("Portrait Editor: automatic identity transfer preserved; saved=")+(written?"yes":"no"));
}
void CaptureIdentitySafely(Character* c,IdentityTransfer& snapshot){
 try{CaptureIdentity(c,snapshot);}
 catch(const std::exception& e){snapshot.captured=false;ErrorLog(std::string("Portrait Editor identity capture: ")+e.what());}
}
void RestoreIdentitySafely(Character* c,const IdentityTransfer& snapshot){
 try{RestoreIdentity(c,snapshot);}
 catch(const std::exception& e){ErrorLog(std::string("Portrait Editor identity restore: ")+e.what());}
}
void (*originalCharacterHandle)(Character*,const hand&)=0;
void CharacterHandle(Character* c,const hand& incoming){
 if(!c)return;
 IdentityTransfer snapshot;CaptureIdentitySafely(c,snapshot);
 {IdentityScope scope(snapshot);originalCharacterHandle(c,incoming);}
 RestoreIdentitySafely(c,snapshot);
}
bool (*originalAddActive)(ActivePlatoon*,RootObject*)=0;
bool AddActive(ActivePlatoon* squad,RootObject* object){
 if(!squad)return false;
 // The native ActivePlatoon insertion contract accepts live characters
 // (its body accesses Character fields), unlike RootObjectContainer insertion.
 Character* c=static_cast<Character*>(object);
 IdentityTransfer snapshot;CaptureIdentitySafely(c,snapshot);
 bool result;{IdentityScope scope(snapshot);result=originalAddActive(squad,object);}
 if(result)RestoreIdentitySafely(c,snapshot);
 return result;
}
void (*originalAddAt)(ActivePlatoon*,RootObject*,int)=0;
void AddAt(ActivePlatoon* squad,RootObject* object,int index){
 if(!squad)return;
 Character* c=static_cast<Character*>(object);
 IdentityTransfer snapshot;CaptureIdentitySafely(c,snapshot);
 {IdentityScope scope(snapshot);originalAddAt(squad,object,index);}
 RestoreIdentitySafely(c,snapshot);
}

// Preserve the transaction at the portrait drag/drop boundary. A transfer can
// update IDs after the low-level membership setter has returned.
void (*originalDropPortrait)(SquadManagementScreen*,wraps::BaseLayout*,wraps::DDItemInfo,bool)=0;
void DropPortrait(SquadManagementScreen* screen,wraps::BaseLayout* sender,wraps::DDItemInfo info,bool accepted){
 if(!screen)return;
 if(!pluginReady || failed || !accepted || !info.sender || !info.receiver){originalDropPortrait(screen,sender,info,accepted);return;}
 Settings saved;std::string beforeKey;Character* previousAddress=0;ActivePlatoon* destination=0;
 std::vector<hand> priorMembers;bool preserve=false,wasEditing=false,wasPending=false;
 try{
  // These layouts are the portrait boxes supplied to this native callback.
  SquadManagementScreen::PortraitSquadItemBox* from=static_cast<SquadManagementScreen::PortraitSquadItemBox*>(info.sender);
  SquadManagementScreen::PortraitSquadItemBox* to=static_cast<SquadManagementScreen::PortraitSquadItemBox*>(info.receiver);
  int sourceCount=from->getItemBox()?from->getItemCount():0;
  if(from->getItemBox() && to->getItemBox() && sourceCount>0 && sourceCount<=1024 && info.sender_index<size_t(sourceCount) && to->squad && to->squad->platoon){
   PortraitData** data=from->getItemDataAt<PortraitData*>(info.sender_index,false);
   Character* c=data && *data ? (*data)->getCharacter() : 0;
   if(c && c->isPlayerCharacter()){
    previousAddress=c;beforeKey=Key(c);saved=Get(c);destination=to->squad->platoon;
    char stored[128]={0};GetPrivateProfileStringA(beforeKey.c_str(),"view","",stored,sizeof(stored),config.c_str());
    Settings defaults;preserve=stored[0] || saved.yaw!=defaults.yaw || saved.pitch!=defaults.pitch || saved.height!=defaults.height || saved.horizontal!=defaults.horizontal || saved.zoom!=defaults.zoom || saved.background!=0;
    wasEditing=editing.getCharacter()==c;wasPending=pending.getCharacter()==c;
    lektor<RootObject*>* things=destination->getThings();
    if(things && things->size()<=1024){
     for(unsigned int i=0;i<things->size();++i)if((*things)[i])priorMembers.push_back((*things)[i]->getHandle());
    }else preserve=false;
   }
  }
 }catch(const std::exception& e){preserve=false;ErrorLog(std::string("Portrait Editor drop capture: ")+e.what());}
 DebugLog(std::string("Portrait Editor: portrait drop captured, applied settings=")+(preserve?"yes":"no"));
 originalDropPortrait(screen,sender,info,accepted);
 if(!preserve || !destination)return;
 try{
  // Reacquire the destination from the current squad table. No source widget
  // or old Character pointer is dereferenced after the native drop callback.
  bool live=false;
  for(std::map<ActivePlatoon*,SquadManagementScreen::SquadData*,std::less<ActivePlatoon*>,Ogre::STLAllocator<std::pair<ActivePlatoon* const,SquadManagementScreen::SquadData*>,Ogre::GeneralAllocPolicy> >::iterator i=screen->squads.begin();i!=screen->squads.end();++i)
   if(i->first==destination && i->second && i->second->platoon==destination){live=true;break;}
  if(!live)return;
  lektor<RootObject*>* things=destination->getThings();if(!things || things->size()>1024)return;
  Character* exact=0;Character* added=0;unsigned int addedCount=0;
  for(unsigned int i=0;i<things->size();++i){
   RootObject* object=(*things)[i];if(!object)continue;
   hand h=object->getHandle();Character* c=h.getCharacter();if(!c || !c->isPlayerCharacter())continue;
   if(c==previousAddress)exact=c;
   bool existed=false;for(size_t j=0;j<priorMembers.size();++j)if(priorMembers[j]==h){existed=true;break;}
   if(!existed){added=c;++addedCount;}
  }
  Character* c=exact?exact:addedCount==1?added:0;
  if(!c){ErrorLog("Portrait Editor: squad drop destination ambiguous; settings not reassigned");return;}
  hand current=c->getHandle();std::string afterKey=Key(c);if(afterKey.empty())return;
  Store(c,saved);nativeUV.erase(afterKey);nativeTextures.erase(afterKey);
  if(wasEditing)editing=current;if(wasPending)pending=current;
  pendingSquadRefresh[afterKey]=current;
  DebugLog(std::string("Portrait Editor: drop settings restored, key changed=")+(beforeKey==afterKey?"no":"yes"));
 }catch(const std::exception& e){ErrorLog(std::string("Portrait Editor drop restore: ")+e.what());}
}
void RefreshTransferredPortraits(){
 if(!ou || !ou->initialized)return;
 std::map<std::string,hand> queued;queued.swap(pendingSquadRefresh);
 size_t processed=0;
 for(std::map<std::string,hand>::iterator i=queued.begin();i!=queued.end();++i){
  if(processed++>=8){pendingSquadRefresh[i->first]=i->second;continue;}
  Character* c=i->second.getCharacter();if(!c || !c->isPlayerCharacter())continue;
  PortraitManager* pm=PortraitManager::getInstance();
  if(pm){PortraitData* portrait=pm->getPortrait(i->second);if(portrait){portrait->reloadPortrait=true;portrait->forceUpdate();}}
  c=i->second.getCharacter();if(c && ou && ou->initialized)ou->addPortraitUpdate(c->getHandle());
 }
}
MyGUI::TextBox* Text(MyGUI::Widget* p,int x,int y,int w,int h,const std::string& caption){MyGUI::TextBox* t=p->createWidget<MyGUI::TextBox>("Kenshi_TextboxStandardText",MyGUI::IntCoord(x,y,w,h),MyGUI::Align::Default);t->setCaption(caption);t->setTextColour(MyGUI::Colour(.82f,.79f,.70f));t->setNeedMouseFocus(false);return t;}
void Background(){
 if(!EditorControlsReady() || !MyGUI::RenderManager::getInstancePtr())return;
 if(draft.background<0 || size_t(draft.background)>=BackgroundCount())draft.background=0;
 bgLabel->setCaption(draft.background<6?bgNames[draft.background]:customBackgrounds[draft.background-6]);backdrop->setVisible(false);
 bool colour=draft.background==4,chosen=draft.background!=0;
 red->setVisible(colour);green->setVisible(colour);blue->setVisible(colour);redLabel->setVisible(colour);greenLabel->setVisible(colour);blueLabel->setVisible(colour);
 brightness->setVisible(chosen);opacity->setVisible(chosen);brightnessLabel->setVisible(chosen);opacityLabel->setVisible(chosen);
 int row=colour?714:602;
 brightness->setPosition(140,row-3);brightnessLabel->setPosition(20,row);
 opacity->setPosition(140,row+37);opacityLabel->setPosition(20,row+40);
 int footer=colour?794:chosen?682:602;
 status->setPosition(20,footer);resetButton->setPosition(20,footer+30);cancelButton->setPosition(132,footer+30);applyButton->setPosition(244,footer+30);
 window->setSize(430,footer+70);
 LayoutNumbers();
 MyGUI::IntSize view=MyGUI::RenderManager::getInstance().getViewSize();MyGUI::IntPoint position=window->getPosition();if(position.top+window->getHeight()>view.height)window->setPosition(position.left,std::max(0,view.height-window->getHeight()));
}
void Sync(){if(!EditorControlsReady())return;
 draft=Sanitize(draft,BackgroundCount());
 struct SyncScope{bool previous;SyncScope():previous(updating){updating=true;}~SyncScope(){updating=previous;}} scope;rotation->setScrollPosition(size_t(draft.yaw+180));tilt->setScrollPosition(size_t(draft.pitch+35));height->setScrollPosition(size_t((draft.height+1)*100));horizontal->setScrollPosition(size_t((draft.horizontal+1)*100));zoom->setScrollPosition(size_t((draft.zoom-.6f)*100));red->setScrollPosition(size_t(draft.r*255));green->setScrollPosition(size_t(draft.g*255));blue->setScrollPosition(size_t(draft.b*255));brightness->setScrollPosition(size_t(draft.brightness*100));opacity->setScrollPosition(size_t(draft.opacity*100));SyncNumbers();Background();}
void Changed(){dirty=true;changedAt=GetTickCount();}
void SliderImpl(MyGUI::ScrollBar* sender,size_t position){if(updating || !opened || !EditorControlsReady() || !sender)return;size_t range=sender->getScrollRange();if(!range)return;position=std::min(position,range-1);if(sender==rotation)draft.yaw=float(position)-180;else if(sender==tilt)draft.pitch=float(position)-35;else if(sender==height)draft.height=float(position)/100-1;else if(sender==horizontal)draft.horizontal=float(position)/100-1;else if(sender==zoom)draft.zoom=.6f+float(position)/100;else if(sender==red)draft.r=float(position)/255;else if(sender==green)draft.g=float(position)/255;else if(sender==blue)draft.b=float(position)/255;else if(sender==brightness)draft.brightness=float(position)/100;else if(sender==opacity)draft.opacity=float(position)/100;else return;if(draft.background==4)Background();SyncNumbers();Changed();}
void UIError(const char* context,const std::exception& e){
 opened=false;updating=false;portraitMenuRequested=false;
 if(window)window->setVisible(false);if(popup)popup->setVisible(false);
 ErrorLog(std::string(context)+e.what());
}
void Slider(MyGUI::ScrollBar* sender,size_t position){
 if(!pluginReady || failed)return;
 try{SliderImpl(sender,position);}catch(const std::exception& e){UIError("Portrait Editor slider: ",e);}
}
void DispatchButton(MyGUI::Widget* sender){
 if(!pluginReady || failed || !sender)return;
 std::map<MyGUI::Widget*,ButtonAction>::const_iterator i=buttonActions.find(sender);if(i==buttonActions.end())return;
 ButtonAction action=i->second;
 try{action(sender);}catch(const std::exception& e){UIError("Portrait Editor button: ",e);}
}

float* NumberValue(MyGUI::ScrollBar* slider){
 if(slider==rotation)return &draft.yaw;if(slider==tilt)return &draft.pitch;if(slider==height)return &draft.height;if(slider==horizontal)return &draft.horizontal;if(slider==zoom)return &draft.zoom;
 if(slider==red)return &draft.r;if(slider==green)return &draft.g;if(slider==blue)return &draft.b;if(slider==brightness)return &draft.brightness;if(slider==opacity)return &draft.opacity;return 0;
}
float NumberScale(MyGUI::ScrollBar* slider){return slider==red || slider==green || slider==blue?255.f:slider==brightness || slider==opacity?100.f:1.f;}
void LayoutNumbers(){
 for(std::map<MyGUI::ScrollBar*,MyGUI::EditBox*>::iterator i=numberFields.begin();i!=numberFields.end();++i){
  i->second->setPosition(350,i->first->getTop()+3);i->second->setVisible(i->first->getVisible());
 }
}
void SyncNumbers(){
 for(std::map<MyGUI::ScrollBar*,MyGUI::EditBox*>::iterator i=numberFields.begin();i!=numberFields.end();++i){
  float* value=NumberValue(i->first);if(!value)continue;
  std::ostringstream out;out<<std::fixed<<std::setprecision(3)<<*value*NumberScale(i->first);
  std::string text=out.str();while(!text.empty() && text[text.size()-1]=='0')text.erase(text.size()-1);if(!text.empty() && text[text.size()-1]=='.')text.erase(text.size()-1);
  i->second->setOnlyText(text);
 }
}
void AcceptNumber(MyGUI::EditBox* sender){
 if(!pluginReady || failed || !opened || updating || !sender || !EditorControlsReady())return;
 try{
  MyGUI::ScrollBar* slider=0;for(std::map<MyGUI::ScrollBar*,MyGUI::EditBox*>::iterator i=numberFields.begin();i!=numberFields.end();++i)if(i->second==sender){slider=i->first;break;}
  float* value=NumberValue(slider);if(!slider || !value)return;
  std::string text=sender->getOnlyText().asUTF8();std::replace(text.begin(),text.end(),',','.');
  std::istringstream input(text);float entered=0;char trailing=0;
  if(!(input>>entered) || (input>>trailing) || !_finite(entered)){SyncNumbers();return;}
  *value=entered/NumberScale(slider);draft=Sanitize(draft,BackgroundCount());Sync();Changed();
 }catch(const std::exception& e){UIError("Portrait Editor numeric input: ",e);}
}
void BlurNumber(MyGUI::Widget* sender,MyGUI::Widget*){if(sender)AcceptNumber(sender->castType<MyGUI::EditBox>(false));}
void CommitFocusedNumber(){
 MyGUI::InputManager* input=MyGUI::InputManager::getInstancePtr();if(!input)return;
 MyGUI::Widget* focused=input->getKeyFocusWidget();
 for(std::map<MyGUI::ScrollBar*,MyGUI::EditBox*>::iterator i=numberFields.begin();i!=numberFields.end();++i)if(i->second==focused){AcceptNumber(i->second);return;}
}

void BgNext(MyGUI::Widget*){draft.background=int((draft.background+1)%BackgroundCount());Background();Changed();}
void BgPrevious(MyGUI::Widget*){draft.background=int((draft.background+BackgroundCount()-1)%BackgroundCount());Background();Changed();}
void Reset(MyGUI::Widget*){draft=Settings();Sync();Changed();}
void Front(MyGUI::Widget*){draft.yaw=0;draft.pitch=0;Sync();Changed();}
void ThreeQuarter(MyGUI::Widget*){draft.yaw=35;draft.pitch=0;Sync();Changed();}
void QueuePortrait(){PortraitManager* pm=PortraitManager::getInstance();if(pm && editing.getCharacter()){PortraitData* pd=pm->getPortrait(editing);if(pd){pd->reloadPortrait=true;pd->forceUpdate();}}if(ou && editing.getCharacter())ou->addPortraitUpdate(editing);}
void Cancel(MyGUI::Widget*){opened=false;if(window)window->setVisible(false);QueuePortrait();}
void Apply(MyGUI::Widget*){CommitFocusedNumber();Character* c=editing.getCharacter();if(!c)return;if(!Store(c,draft)){if(status)status->setCaption("Could not save settings. Check RE_Kenshi_log.txt.");return;}opened=false;if(window)window->setVisible(false);QueuePortrait();}
MyGUI::Button* Button(MyGUI::Widget* p,int x,int y,int w,const char* label,void (*callback)(MyGUI::Widget*)){MyGUI::Button* b=p->createWidget<MyGUI::Button>("Kenshi_Button1",MyGUI::IntCoord(x,y,w,28),MyGUI::Align::Default);b->setCaption(label);buttonActions[b]=callback;b->eventMouseButtonClick+=MyGUI::newDelegate(&DispatchButton);return b;}
MyGUI::ScrollBar* SliderRow(int y,const char* label,size_t range,MyGUI::TextBox** labelOut=0){MyGUI::TextBox* t=Text(window,20,y,110,25,label);if(labelOut)*labelOut=t;MyGUI::ScrollBar* s=window->createWidget<MyGUI::ScrollBar>("Kenshi_SliderHBig",MyGUI::IntCoord(140,y-3,200,34),MyGUI::Align::Default);MyGUI::Widget* start=s->findWidget("Start");MyGUI::Widget* end=s->findWidget("End");
 if(start){start->changeWidgetSkin("Kenshi_Button1");MyGUI::Button* b=start->castType<MyGUI::Button>(false);if(b)b->setCaption("<");}
 if(end){end->changeWidgetSkin("Kenshi_Button1");MyGUI::Button* b=end->castType<MyGUI::Button>(false);if(b)b->setCaption(">");}
 s->setScrollRange(range);s->setScrollPage(1);s->setMinTrackSize(36);s->setTrackSize(36);s->setMoveToClick(true);s->eventScrollChangePosition+=MyGUI::newDelegate(&Slider);
 MyGUI::EditBox* number=window->createWidget<MyGUI::EditBox>("Kenshi_EditBoxEmptySkin",MyGUI::IntCoord(350,y,66,28),MyGUI::Align::Default);
 number->setFontName("Kenshi_StandardFont_Medium");number->setTextColour(MyGUI::Colour(.82f,.79f,.70f));number->setTextAlign(MyGUI::Align::Center);number->setMaxTextLength(16);number->setEditMultiLine(false);number->setEditWordWrap(false);number->setNeedKeyFocus(true);number->setNeedMouseFocus(true);
 number->eventEditSelectAccept+=MyGUI::newDelegate(&AcceptNumber);number->eventKeyLostFocus+=MyGUI::newDelegate(&BlurNumber);numberFields[s]=number;
 return s;}
void OpenEditor(MyGUI::Widget*){
 if(!pluginReady || failed)return;
 // Copy the target before closing native menus; callbacks can clear pending state.
 hand target=pending;
 try{
  EnsureUI();if(!EditorControlsReady())return;
  portraitMenuRequested=false;extensionShown=false;
  if(ou)ou->hideContextMenu();if(opened)Cancel(0);
  Character* c=target.getCharacter();if(!c || !c->isPlayerCharacter() || !EditorControlsReady())return;
  editing=target;draft=Get(c);opened=true;
  if(popup)popup->setVisible(false);window->setVisible(true);title->setCaption("PORTRAIT - "+c->getName());Sync();
  PortraitManager* pm=PortraitManager::getInstance();if(pm && preview)pm->setImageWidget(editing,preview,true);Changed();
 }catch(const std::exception& e){opened=false;if(window)window->setVisible(false);ErrorLog(std::string("Portrait Editor open: ")+e.what());}
}

void Create(){
 MyGUI::Gui* g=MyGUI::Gui::getInstancePtr();if(!g || !MyGUI::RenderManager::getInstancePtr())return;
 if(!lifetimeRegistered){MyGUI::WidgetManager::getInstance().registerUnlinker(&widgetLifetime);lifetimeRegistered=true;}
 Resources();
 MyGUI::IntSize view=MyGUI::RenderManager::getInstance().getViewSize();
 window=g->createWidget<MyGUI::Widget>("Kenshi_GenericTextBoxSkin",MyGUI::IntCoord(std::max(0,(view.width-430)/2),std::max(0,(view.height-700)/2),430,700),MyGUI::Align::Default,"Window","PortraitEditorWindow");
 title=Text(window,15,10,360,28,"PORTRAIT");Button(window,385,8,28,"X",Cancel);
 MyGUI::Widget* square=window->createWidget<MyGUI::Widget>("PanelEmpty",MyGUI::IntCoord(87,48,256,256),MyGUI::Align::Default);
 backdrop=square->createWidget<MyGUI::ImageBox>("ImageBox",MyGUI::IntCoord(0,0,256,256),MyGUI::Align::Stretch);backdrop->setNeedMouseFocus(false);backdrop->setDepth(5);
 preview=square->createWidget<MyGUI::ImageBox>("ImageBox",MyGUI::IntCoord(0,0,256,256),MyGUI::Align::Stretch);preview->setNeedMouseFocus(false);preview->setDepth(3);
 rotation=SliderRow(325,"Rotation",361);tilt=SliderRow(365,"Tilt",71);height=SliderRow(405,"Camera height",201);horizontal=SliderRow(445,"Horizontal",201);zoom=SliderRow(485,"Zoom",121);
 Button(window,55,527,110,"Front",Front);Button(window,190,527,110,"3/4 view",ThreeQuarter);
 Text(window,20,567,110,24,"Background");Button(window,133,564,28,"<",BgPrevious);bgLabel=Text(window,168,567,140,24,"");Button(window,315,564,28,">",BgNext);
 red=SliderRow(602,"Red",256,&redLabel);green=SliderRow(638,"Green",256,&greenLabel);blue=SliderRow(674,"Blue",256,&blueLabel);
 brightness=SliderRow(714,"Brightness",201,&brightnessLabel);opacity=SliderRow(754,"Opacity",101,&opacityLabel);
 status=Text(window,20,635,325,22,"Preview updates when you release a control.");
 resetButton=Button(window,20,660,96,"Reset",Reset);cancelButton=Button(window,132,660,96,"Cancel",Cancel);applyButton=Button(window,244,660,96,"Apply",Apply);window->setVisible(false);
 popup=g->createWidget<MyGUI::Widget>("PanelEmpty",MyGUI::IntCoord(0,0,190,40),MyGUI::Align::Default,"Popup","PortraitEditorContext");popupAction=Button(popup,3,3,120,"Edit portrait",OpenEditor);popupAction->setTextAlign(MyGUI::Align::Center);
 int popupFont=popupAction->getFontHeight();if(popupFont<=0)popupFont=popupAction->getTextSize().height;if(popupFont<=0)popupFont=18;
 popupAction->setFontName("Kenshi_StandardFont_Medium");popupAction->setFontHeight(18);popupAction->setTextColour(MyGUI::Colour(.92f,.89f,.80f));popup->setVisible(false);
}
void (*originalNotify)(MainBarGUI*,MyGUI::ItemBox*,const MyGUI::IBNotifyItemData&)=0;
void Notify(MainBarGUI* bar,MyGUI::ItemBox* box,const MyGUI::IBNotifyItemData& info){
 if(!bar || !box)return;
 // Determine whether this was already selected BEFORE the native click handler.
 hand clicked;MyGUI::IntPoint position;bool alreadySelected=false;DWORD pressedAt=GetTickCount();
 try{
  if(pluginReady && !failed && MyGUI::InputManager::getInstancePtr() && info.notify==MyGUI::IBNotifyItemData::MousePressed && info.id==MyGUI::MouseButton::Right && info.index<box->getItemCount()){
   PortraitData** pp=box->getItemDataAt<PortraitData*>(info.index,false);
   Character* c=pp && *pp?(*pp)->getCharacter():0;
   if(c && c->isPlayerCharacter()){
    clicked=c->getHandle();position=MyGUI::InputManager::getInstance().getMousePosition();
    alreadySelected=ou && ou->initialized && ou->player && ou->player->selectedCharacter.getCharacter()==c;
   }
  }
 }catch(const std::exception& e){ErrorLog(std::string("Portrait Editor click capture: ")+e.what());clicked.setNull();}
 originalNotify(bar,box,info);
 if(clicked.getCharacter()){
  pending=clicked;portraitMenuPosition=position;portraitMenuRequested=true;portraitMenuRequestedAt=pressedAt;
  portraitHoldEligible=alreadySelected;portraitHoldComplete=false;portraitHoldReleasedEarly=false;
  extensionShown=false;nativeMenuSeen=false;if(popup)popup->setVisible(false);
 }
 if(pluginReady && !failed && portraitMenuRequested && info.notify==MyGUI::IBNotifyItemData::MouseReleased && info.id==MyGUI::MouseButton::Right && !portraitHoldComplete){
  // Handle a release between UI polls without mistaking a quick click for a hold.
  Character* c=pending.getCharacter();
  if(portraitHoldEligible && c && ou && ou->player && ou->player->selectedCharacter.getCharacter()==c && GetTickCount()-portraitMenuRequestedAt>=250)portraitHoldComplete=true;
  else portraitHoldReleasedEarly=true;
 }
}

void PollPortraitMenu(){
 if(failed || !portraitMenuRequested || !ou || !ou->initialized || !ou->player || !MyGUI::InputManager::getInstancePtr() || !MyGUI::RenderManager::getInstancePtr()){if(popup)popup->setVisible(false);return;}
 try{
  Character* character=pending.getCharacter();
  if(!character || !character->isPlayerCharacter() || (GetAsyncKeyState(VK_ESCAPE)&0x8000)){
   portraitMenuRequested=false;if(popup)popup->setVisible(false);return;
  }
  ContextMenuGUI* choices[]={ou->player->contextMenu.menuGUI,ou->player->contextMenu.menuGUI2};ContextMenuGUI* native=0;
  for(int i=0;i<2;++i)if(choices[i] && choices[i]->getVisible()){native=choices[i];break;}
  if(!native && nativeMenuSeen){portraitMenuRequested=false;if(popup)popup->setVisible(false);return;}
  // Native actions keep their original timing. Only the standalone action
  // requires a continuous hold on the already-selected character.
  if(!native){
   if(!portraitHoldEligible || portraitHoldReleasedEarly || ou->player->selectedCharacter.getCharacter()!=character){
    // Native menus may be created on release, not press. Keep the native-only
    // request briefly armed; it can never open a standalone edit action.
    if(!(GetAsyncKeyState(VK_RBUTTON)&0x8000) && GetTickCount()-portraitMenuRequestedAt>=2000)portraitMenuRequested=false;
    if(popup)popup->setVisible(false);return;
   }
   if(!portraitHoldComplete){
    if(GetAsyncKeyState(VK_LBUTTON)&0x8000){portraitMenuRequested=false;if(popup)popup->setVisible(false);return;}
    if(!(GetAsyncKeyState(VK_RBUTTON)&0x8000)){portraitHoldReleasedEarly=true;if(popup)popup->setVisible(false);return;}
    if(GetTickCount()-portraitMenuRequestedAt<250){if(popup)popup->setVisible(false);return;}
    portraitHoldComplete=true;
   }
  }
  EnsureUI();if(!EditorControlsReady() || !popup || !popupAction)return;
  // Measure the active skin/font instead of tying this action to menu width.
  MyGUI::IntSize text=popupAction->getTextSize();MyGUI::IntCoord region=popupAction->getTextRegion();
  int textWidth=text.width>0 && text.width<2048?text.width:66;
  int textHeight=text.height>0 && text.height<512?text.height:12;
  int chromeWidth=std::max(0,std::min(64,popupAction->getWidth()-region.width));
  int chromeHeight=std::max(0,std::min(64,popupAction->getHeight()-region.height));
  int width=textWidth+chromeWidth+10,popupHeight=textHeight+chromeHeight+6;
  int x=portraitMenuPosition.left,y=portraitMenuPosition.top-popupHeight-2;
  if(native){
   MyGUI::Widget* root=native->getMainWidget();if(!root)return;
   MyGUI::IntCoord rect=root->getAbsoluteCoord();
   x=rect.left+(rect.width-width)/2;y=rect.top-popupHeight-2;nativeMenuSeen=true;
  }
  MyGUI::IntSize view=MyGUI::RenderManager::getInstance().getViewSize();
  x=std::max(0,std::min(x,std::max(0,view.width-width)));y=std::max(0,std::min(y,std::max(0,view.height-popupHeight)));
  popup->setCoord(x,y,width,popupHeight);popupAction->setCoord(2,2,width-4,popupHeight-4);
  if(extensionShown && (GetAsyncKeyState(VK_LBUTTON)&0x8000)){
   MyGUI::IntPoint mouse=MyGUI::InputManager::getInstance().getMousePosition();
   if(mouse.left<x || mouse.left>=x+width || mouse.top<y || mouse.top>=y+popupHeight){portraitMenuRequested=false;popup->setVisible(false);return;}
  }
  popup->setVisible(true);extensionShown=true;
 }catch(const std::exception& e){portraitMenuRequested=false;ErrorLog(std::string("Portrait Editor menu placement: ")+e.what());if(popup)popup->setVisible(false);}
}

void (*originalCell)(PortraitMainCellView*,const MyGUI::IBDrawItemInfo&,PortraitData*)=0;
void SetComposed(Character* c,MyGUI::ImageBox* image){
 if(!c || !image || Effective(c).background==0)return;
 std::string key=Key(c);std::map<std::string,Ogre::TexturePtr>::iterator i=composed.find(key);
 if(i!=composed.end() && !i->second.isNull()){
  image->setImageInfo(i->second->getName(),MyGUI::IntCoord(0,0,i->second->getWidth(),i->second->getHeight()),MyGUI::IntSize(i->second->getWidth(),i->second->getHeight()));image->setImageIndex(0);
 }else if(ou && !key.empty() && GetTickCount()-requested[key]>1000){requested[key]=GetTickCount();ou->addPortraitUpdate(c->getHandle());}
}
void Cell(PortraitMainCellView* cell,const MyGUI::IBDrawItemInfo& info,PortraitData* data){
 if(!cell)return;
 originalCell(cell,info,data);if(!pluginReady || failed || !data || !cell->imagePortrait)return;Character* c=data->getCharacter();if(c && c->isPlayerCharacter())SetComposed(c,cell->imagePortrait);
}
void (*originalUpdate)(MainBarGUI*)=0;
void Update(MainBarGUI* bar){
 if(!bar)return;
 originalUpdate(bar);if(failed || !pluginReady)return;
 identityThread=GetCurrentThreadId();
 try{
  RefreshTransferredPortraits();
  PollPortraitMenu();
  // Drain a snapshot. Rendering may queue more composition work during callbacks.
  std::map<std::string,hand> queued;queued.swap(pendingComposition);
  size_t compositionBudget=8;
  for(std::map<std::string,hand>::iterator i=queued.begin();i!=queued.end();++i){
   if(!compositionBudget){if(!pendingComposition.count(i->first))pendingComposition[i->first]=i->second;continue;}
   --compositionBudget;Character* c=i->second.getCharacter();if(c)ComposeRegistered(c);
  }
  if(!opened || !EditorControlsReady())return;
  Character* c=editing.getCharacter();if(!c){Cancel(0);return;}
  if(dirty && GetTickCount()-changedAt>180){dirty=false;QueuePortrait();}
  PortraitManager* pm=PortraitManager::getInstance();if(pm)pm->setImageWidget(editing,preview,true);
  c=editing.getCharacter();if(opened && c && preview)SetComposed(c,preview);
 }catch(const std::exception& e){failed=true;ErrorLog(std::string("Portrait Editor update: ")+e.what());}
}

struct RenderContext {Character* character;Settings settings;Ogre::Camera* camera;Ogre::Vector3 position,focus;Ogre::Quaternion orientation;Ogre::Radian fov;bool adjusted,hasFocus;RenderContext():character(0),camera(0),adjusted(false),hasFocus(false){} };
__declspec(thread) RenderContext* active=0;
__declspec(thread) bool modifying=false;
struct ModifyScope {bool previous;ModifyScope():previous(modifying){modifying=true;}~ModifyScope(){modifying=previous;}};
void (*originalLook)(Ogre::Camera*,const Ogre::Vector3&)=0;
void (*originalLookXYZ)(Ogre::Camera*,float,float,float)=0;
const Ogre::Matrix4& (*originalView)(const Ogre::Camera*)=0;
const Ogre::Matrix4& (*originalViewOwn)(const Ogre::Camera*,bool)=0;
void Look(Ogre::Camera* camera,const Ogre::Vector3& target){
 if(!camera)return;
 originalLook(camera,target);
 if(active && !modifying && !active->adjusted){active->camera=camera;active->focus=target;active->hasFocus=true;}
}
void LookXYZ(Ogre::Camera* camera,float x,float y,float z){
 if(!camera)return;
 originalLookXYZ(camera,x,y,z);
 if(active && !modifying && !active->adjusted){active->camera=camera;active->focus=Ogre::Vector3(x,y,z);active->hasFocus=true;}
}
void Adjust(const Ogre::Camera* source){
 RenderContext* ctx=active;if(!pluginReady || failed || !ctx || modifying || ctx->adjusted || !source)return;
 Ogre::Camera* camera=const_cast<Ogre::Camera*>(source);
 if(ctx->camera && ctx->camera!=camera)return;
 ctx->camera=camera;ctx->position=camera->getPosition();ctx->orientation=camera->getOrientation();ctx->fov=camera->getFOVy();ctx->adjusted=true;
 Ogre::Vector3 focus=ctx->hasFocus?ctx->focus:ctx->position+camera->getDirection()*10.f;
 Ogre::Quaternion turn(Ogre::Degree(ctx->settings.yaw),Ogre::Vector3::UNIT_Y);
 Ogre::Vector3 right=turn*(ctx->orientation*Ogre::Vector3::UNIT_X);
 Ogre::Quaternion pitch(Ogre::Degree(ctx->settings.pitch),right);
 Ogre::Quaternion orbit=pitch*turn;
 Ogre::Vector3 offset=Ogre::Vector3(0,ctx->settings.height,0)+right*ctx->settings.horizontal;
 ModifyScope scope;
 camera->setPosition(focus+orbit*(ctx->position-focus)+offset);
 originalLook(camera,focus+offset);
 camera->setFOVy(Ogre::Radian(ctx->fov.valueRadians()/ctx->settings.zoom));

}
const Ogre::Matrix4& View(const Ogre::Camera* camera){Adjust(camera);return originalView(camera);}
const Ogre::Matrix4& ViewOwn(const Ogre::Camera* camera,bool own){Adjust(camera);return originalViewOwn(camera,own);}
void Restore(RenderContext& ctx){if(ctx.adjusted && ctx.camera){ModifyScope scope;ctx.camera->setPosition(ctx.position);ctx.camera->setOrientation(ctx.orientation);ctx.camera->setFOVy(ctx.fov);}}
void Compose(Character* c,Ogre::SharedPtr<Ogre::Texture> atlas,const iVector2& tileSize,const Ogre::TRect<float>& rect){
 Settings s=Sanitize(Effective(c),BackgroundCount());if(s.background==0 || atlas.isNull())return;std::string key=Key(c);if(key.empty())return;
 try{
  size_t w=atlas->getWidth(),h=atlas->getHeight();
  if(!w || !h || w>4096 || h>4096 || !_finite(rect.left) || !_finite(rect.top) || !_finite(rect.right) || !_finite(rect.bottom))return;
  if(composed.size()>=256 && !composed.count(key))return;
  if(rect.left<0 || rect.top<0 || rect.right>1 || rect.bottom>1 || rect.right<=rect.left || rect.bottom<=rect.top)return;
  size_t left=size_t(std::max(0.f,rect.left)*w+.5f),top=size_t(std::max(0.f,rect.top)*h+.5f);
  size_t right=size_t(std::min(1.f,rect.right)*w+.5f),bottom=size_t(std::min(1.f,rect.bottom)*h+.5f);
  if(right<=left || bottom<=top || right>w || bottom>h || right-left>512 || bottom-top>512){ErrorLog("Portrait Editor: invalid atlas tile rectangle");return;}
  size_t tw=right-left,th=bottom-top;std::vector<unsigned char> pixels(tw*th*4);
  Ogre::PixelBox box(tw,th,1,Ogre::PF_BYTE_RGBA,&pixels[0]);
  std::vector<unsigned char> sheet(w*h*4);
  Ogre::PixelBox fullSheet(w,h,1,Ogre::PF_BYTE_RGBA,&sheet[0]);
  atlas->getBuffer()->blitToMemory(fullSheet);
  for(size_t y=0;y<th;++y)std::copy(sheet.begin()+((top+y)*w+left)*4,sheet.begin()+((top+y)*w+right)*4,pixels.begin()+y*tw*4);
  float r=s.background==1?32.f/255:s.background==2?0:s.background==3?139.f/255:s.background==5?93.f/255:s.r;
  float g=s.background==1?91.f/255:s.background==2?0:s.background==3?116.f/255:s.background==5?36.f/255:s.g;
  float b=s.background==1?88.f/255:s.background==2?0:s.background==3?84.f/255:s.background==5?52.f/255:s.b;
  const std::vector<unsigned char>* scenery=0;
  if(s.background>=6){
   if(size_t(s.background-6)>=customBackgrounds.size())return;
   Resources();size_t cacheBytes=0;for(std::map<std::string,std::vector<unsigned char> >::const_iterator i=backgroundPixels.begin();i!=backgroundPixels.end();++i)cacheBytes+=i->second.size();
   if(cacheBytes+tw*th*3>64u*1024u*1024u)backgroundPixels.clear();
   std::string filename=customBackgrounds[s.background-6];std::ostringstream cacheName;cacheName<<filename<<':'<<tw<<'x'<<th;std::vector<unsigned char>& cached=backgroundPixels[cacheName.str()];
   if(cached.empty()){
    Ogre::Image image;image.load(filename,"General");size_t iw=image.getWidth(),ih=image.getHeight();if(!iw || !ih || iw>2048 || ih>2048){ErrorLog("Portrait Editor: background must be 2048x2048 or smaller");return;}
    size_t side=std::min(iw,ih),ox=(iw-side)/2,oy=(ih-side)/2;std::vector<unsigned char> generated(tw*th*3);
    for(size_t y=0;y<th;++y)for(size_t x=0;x<tw;++x){Ogre::ColourValue colour=image.getColourAt(ox+x*side/tw,oy+y*side/th,0);size_t n=(y*tw+x)*3;generated[n]=static_cast<unsigned char>(Bounded(colour.r,0,1,0)*255);generated[n+1]=static_cast<unsigned char>(Bounded(colour.g,0,1,0)*255);generated[n+2]=static_cast<unsigned char>(Bounded(colour.b,0,1,0)*255);}
    cached.swap(generated);
   }
   if(cached.size()!=tw*th*3)return;
   scenery=&cached;
  }
  for(size_t n=0;n<pixels.size();n+=4){
   float characterAlpha=pixels[n+3]/255.f;size_t bn=n/4*3;
   float background[3]={scenery?float((*scenery)[bn]):r*255,scenery?float((*scenery)[bn+1]):g*255,scenery?float((*scenery)[bn+2]):b*255};
   float visibleBackground=s.opacity*(1-characterAlpha),outputAlpha=characterAlpha+visibleBackground;
   for(int channel=0;channel<3;++channel){float colour=background[channel];colour=s.brightness<=1?colour*s.brightness:colour+(255-colour)*(s.brightness-1);float output=outputAlpha>0?(pixels[n+channel]*characterAlpha+colour*visibleBackground)/outputAlpha:0;pixels[n+channel]=static_cast<unsigned char>(std::max(0.f,std::min(255.f,output)));}
   pixels[n+3]=static_cast<unsigned char>(outputAlpha*255+.5f);
  }
  Ogre::TexturePtr& texture=composed[key];
  if(texture.isNull()){std::ostringstream name;name<<"PortraitEditorComposed_"<<++textureSerial;texture=Ogre::TextureManager::getSingleton().createManual(name.str(),"General",Ogre::TEX_TYPE_2D,tw,th,0,Ogre::PF_BYTE_RGBA,Ogre::TU_DYNAMIC_WRITE_ONLY_DISCARDABLE);}
  if(texture->getWidth()!=tw || texture->getHeight()!=th)return;
  texture->getBuffer()->blitFromMemory(box);
  static bool reported=false;if(!reported){reported=true;std::ostringstream message;message<<"Portrait Editor: background composed successfully, tile "<<tw<<"x"<<th;DebugLog(message.str());}
 }catch(const std::exception& e){ErrorLog(std::string("Portrait Editor background composition: ")+e.what());}
}
void ComposeRegistered(Character* c){
 if(!c)return;
 hand saved=c->getHandle();
 PortraitManager* pm=PortraitManager::getInstance();if(!pm || pm->texture.isNull())return;
 std::string key=Key(c);std::map<std::string,Ogre::TRect<float> >::iterator uv=nativeUV.find(key);
 if(uv==nativeUV.end()){
  // Force the normal portrait assignment to expose its actual atlas crop.
  if(opened && editing.getCharacter()==c && preview)pm->setImageWidget(editing,preview,true);
  c=saved.getCharacter();if(!c)return;
  pm=PortraitManager::getInstance();if(!pm || pm->texture.isNull())return;
  uv=nativeUV.find(key);if(uv==nativeUV.end())return;
 }
 Ogre::TexturePtr source=pm->texture;
 std::map<std::string,std::string>::iterator textureName=nativeTextures.find(key);
 if(textureName!=nativeTextures.end()){Ogre::TexturePtr actual=Ogre::TextureManager::getSingleton().getByName(textureName->second);if(!actual.isNull())source=actual;}
 Compose(c,source,pm->texturePortraitSize,uv->second);
 static std::map<std::string,bool> reported;if(!reported[key]){reported[key]=true;std::ostringstream line;line<<"Portrait Editor: source="<<source->getName()<<" size="<<source->getWidth()<<"x"<<source->getHeight()<<" captured native portrait crop for "<<c->getName()<<" = "<<uv->second.left<<","<<uv->second.top<<","<<uv->second.right<<","<<uv->second.bottom;DebugLog(line.str());}
}
void (*originalRender)(RenderToTextureotron*,Character*,Ogre::SharedPtr<Ogre::Texture>,const iVector2&,const Ogre::TRect<float>&)=0;
void Render(RenderToTextureotron* machine,Character* c,Ogre::SharedPtr<Ogre::Texture> texture,const iVector2& size,const Ogre::TRect<float>& rect){
 if(!machine)return;
 if(!pluginReady || failed || !c || !c->isPlayerCharacter()){originalRender(machine,c,texture,size,rect);return;}
 hand saved=c->getHandle();
 RenderContext ctx;ctx.character=c;ctx.settings=Sanitize(Effective(c),BackgroundCount());RenderContext* previous=active;active=&ctx;
 try{originalRender(machine,c,texture,size,rect);}catch(...){active=previous;Restore(ctx);throw;}
 active=previous;
 Restore(ctx);
 c=saved.getCharacter();if(!c)return;
 if(Effective(c).background!=0)pendingComposition[Key(c)]=c->getHandle();
 static bool warned=false;if(!ctx.adjusted && !warned){warned=true;DebugLog("Portrait Editor: no camera matrix callback during portrait rendering");}
 static bool reached=false;if(ctx.adjusted && !reached){reached=true;DebugLog("Portrait Editor: live portrait camera controls active");}
}
}
__declspec(dllexport) void startPlugin(){
 static bool started=false;if(started)return;started=true;identityThread=GetCurrentThreadId();
 KenshiLib::BinaryVersion version=KenshiLib::GetKenshiVersion();if(version.GetPlatform()!=KenshiLib::BinaryVersion::STEAM || version.GetVersion()!="1.0.65"){ErrorLog("Portrait Editor: unsupported executable; hooks disabled");return;}
 HMODULE module=0;if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(&startPlugin),&module)){ErrorLog("Portrait Editor: module lookup failed");return;}char path[MAX_PATH]={0};DWORD pathLength=GetModuleFileNameA(module,path,MAX_PATH);if(!pathLength || pathLength>=MAX_PATH){ErrorLog("Portrait Editor: module path unavailable");return;}resources=path;resources=resources.substr(0,resources.find_last_of("\\/"))+"/gui";
 backgroundFolder=resources.substr(0,resources.find_last_of("\\/"))+"/backgrounds";ScanBackgrounds();
 char local[MAX_PATH];DWORD length=GetEnvironmentVariableA("LOCALAPPDATA",local,MAX_PATH);if(!length || length>=MAX_PATH){ErrorLog("Portrait Editor: cannot find settings folder");return;}std::string folder=std::string(local)+"/kenshi";if(!CreateDirectoryA(folder.c_str(),0) && GetLastError()!=ERROR_ALREADY_EXISTS){ErrorLog("Portrait Editor: settings directory unavailable");return;}config=folder+"/portrait_editor.ini";
 HMODULE ogre=GetModuleHandleA("OgreMain_x64.dll");
 const char* exports[]={"?lookAt@Camera@Ogre@@QEAAXAEBVVector3@2@@Z","?lookAt@Camera@Ogre@@QEAAXMMM@Z","?getViewMatrix@Camera@Ogre@@UEBAAEBVMatrix4@2@XZ","?getViewMatrix@Camera@Ogre@@QEBAAEBVMatrix4@2@_N@Z"};
 void* targets[4];for(int i=0;i<4;++i){targets[i]=ogre?reinterpret_cast<void*>(GetProcAddress(ogre,exports[i])):0;if(!targets[i]){ErrorLog("Portrait Editor: camera entry unavailable");return;}}
 HMODULE gui=GetModuleHandleA("MyGUIEngine_x64.dll");void* uvTarget=gui?reinterpret_cast<void*>(GetProcAddress(gui,"?_setUVSet@ImageBox@MyGUI@@AEAAXAEBU?$TRect@M@types@2@@Z")):0;
 void* textureTarget=gui?reinterpret_cast<void*>(GetProcAddress(gui,"?setImageTexture@ImageBox@MyGUI@@QEAAXAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z")):0;
 void* setImageTarget=reinterpret_cast<void*>(KenshiLib::GetRealAddress(&PortraitManager::setImageWidget));
 void* renderTarget=reinterpret_cast<void*>(KenshiLib::GetRealAddress(&RenderToTextureotron::renderCharacter));
 void* notifyTarget=reinterpret_cast<void*>(KenshiLib::GetRealAddress(&MainBarGUI::portraitNotifyItem));
 void* cellTarget=reinterpret_cast<void*>(KenshiLib::GetRealAddress(&PortraitMainCellView::update));
 void* squadTarget=reinterpret_cast<void*>(KenshiLib::GetRealAddress(&SquadManagementScreen::notifyEndDropPortrait));
 void* updateTarget=reinterpret_cast<void*>(KenshiLib::GetRealAddress(&MainBarGUI::_NV_update));
 void* handleTarget=reinterpret_cast<void*>(KenshiLib::GetRealAddress(&Character::_NV_setHandle));
 void* addActiveTarget=reinterpret_cast<void*>(KenshiLib::GetRealAddress(&ActivePlatoon::_NV_addActiveObject));
 void* addAtTarget=reinterpret_cast<void*>(KenshiLib::GetRealAddress(&ActivePlatoon::addCharacterAt));
 if(!textureTarget || !uvTarget || !setImageTarget || !renderTarget || !notifyTarget || !cellTarget || !updateTarget || !squadTarget || !handleTarget || !addActiveTarget || !addAtTarget){ErrorLog("Portrait Editor: required hook address unavailable; no hooks installed");return;}
 if(KenshiLib::AddHook(targets[0],&Look,&originalLook)!=KenshiLib::SUCCESS)failed=true;
 if(KenshiLib::AddHook(targets[1],&LookXYZ,&originalLookXYZ)!=KenshiLib::SUCCESS)failed=true;
 if(KenshiLib::AddHook(targets[2],&View,&originalView)!=KenshiLib::SUCCESS)failed=true;
 if(KenshiLib::AddHook(targets[3],&ViewOwn,&originalViewOwn)!=KenshiLib::SUCCESS)failed=true;
 if(KenshiLib::AddHook(textureTarget,&Texture,&originalTexture)!=KenshiLib::SUCCESS)failed=true;
 if(KenshiLib::AddHook(uvTarget,&UV,&originalUV)!=KenshiLib::SUCCESS)failed=true;
 if(KenshiLib::AddHook(setImageTarget,&SetImage,&originalSetImage)!=KenshiLib::SUCCESS)failed=true;
 if(KenshiLib::AddHook(renderTarget,&Render,&originalRender)!=KenshiLib::SUCCESS)failed=true;
 if(KenshiLib::AddHook(notifyTarget,&Notify,&originalNotify)!=KenshiLib::SUCCESS)failed=true;
 if(KenshiLib::AddHook(cellTarget,&Cell,&originalCell)!=KenshiLib::SUCCESS)failed=true;
 if(KenshiLib::AddHook(squadTarget,&DropPortrait,&originalDropPortrait)!=KenshiLib::SUCCESS)failed=true;
 if(KenshiLib::AddHook(updateTarget,&Update,&originalUpdate)!=KenshiLib::SUCCESS)failed=true;
 if(KenshiLib::AddHook(handleTarget,&CharacterHandle,&originalCharacterHandle)!=KenshiLib::SUCCESS)failed=true;
 if(KenshiLib::AddHook(addActiveTarget,&AddActive,&originalAddActive)!=KenshiLib::SUCCESS)failed=true;
 if(KenshiLib::AddHook(addAtTarget,&AddAt,&originalAddAt)!=KenshiLib::SUCCESS)failed=true;
 if(failed){failed=true;ErrorLog("Portrait Editor: hooks could not be installed");}else {pluginReady=true;DebugLog("Portrait Editor loaded: edit portrait; automatic identity-transfer preservation enabled (15 hooks)");}
}


