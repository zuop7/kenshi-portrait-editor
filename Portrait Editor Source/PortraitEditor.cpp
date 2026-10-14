#define NOMINMAX
#include <windows.h>
#include <cmath>
#include <float.h>
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
namespace {
bool pluginReady=false;
struct Settings {float yaw,height,horizontal,zoom,r,g,b,brightness,opacity;int background;Settings():yaw(0),height(0),horizontal(0),zoom(1),r(.25f),g(.25f),b(.25f),brightness(1),opacity(1),background(0){} };
std::map<std::string,Settings> settings;
std::map<std::string,Ogre::TexturePtr> composed;
std::map<std::string,DWORD> requested;
unsigned int textureSerial=0;
std::map<std::string,hand> pendingComposition;
void ComposeRegistered(Character* c);
std::string Key(Character* c);
std::map<std::string,Ogre::TRect<float> > nativeUV;
std::map<std::string,std::string> nativeTextures;
struct Capture {hand character;MyGUI::ImageBox* widget;};
__declspec(thread) Capture* capturing=0;
void (*originalTexture)(MyGUI::ImageBox*,const std::string&)=0;
void Texture(MyGUI::ImageBox* image,const std::string& name){originalTexture(image,name);if(pluginReady && capturing && capturing->widget==image){Character* c=capturing->character.getCharacter();if(c && name.find("PortraitEditorComposed_")!=0)nativeTextures[Key(c)]=name;}}
void (*originalUV)(MyGUI::ImageBox*,const MyGUI::FloatRect&)=0;
void UV(MyGUI::ImageBox* image,const MyGUI::FloatRect& rect){
 originalUV(image,rect);
 if(pluginReady && capturing && capturing->widget==image){Character* c=capturing->character.getCharacter();if(c && rect.right>rect.left && rect.bottom>rect.top)nativeUV[Key(c)]=Ogre::TRect<float>(rect.left,rect.top,rect.right,rect.bottom);}
}
void (*originalSetImage)(PortraitManager*,const hand&,MyGUI::ImageBox*,bool)=0;
void SetImage(PortraitManager* pm,const hand& character,MyGUI::ImageBox* image,bool force){
 if(!pluginReady){originalSetImage(pm,character,image,force);return;}
 Capture capture;capture.character=character;capture.widget=image;Capture* previous=capturing;capturing=&capture;
 try{originalSetImage(pm,character,image,force);}catch(...){capturing=previous;throw;}capturing=previous;
}
std::string resources,config,backgroundFolder;
std::vector<std::string> customBackgrounds;
std::map<std::string,std::vector<unsigned char> > backgroundPixels;
void ScanBackgrounds(){
 WIN32_FIND_DATAA item;HANDLE search=FindFirstFileA((backgroundFolder+"/*").c_str(),&item);if(search==INVALID_HANDLE_VALUE)return;
 do{if(item.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue;std::string name=item.cFileName;size_t dot=name.find_last_of(".");if(dot==std::string::npos)continue;std::string ext=name.substr(dot);std::transform(ext.begin(),ext.end(),ext.begin(),::tolower);if(customBackgrounds.size()<128 && (ext==".png" || ext==".jpg" || ext==".jpeg"))customBackgrounds.push_back(name);}while(FindNextFileA(search,&item));FindClose(search);std::sort(customBackgrounds.begin(),customBackgrounds.end());
}
size_t BackgroundCount(){return 6+customBackgrounds.size();}
MyGUI::Widget *window=0,*popup=0;
MyGUI::ImageBox *preview=0,*backdrop=0;
MyGUI::TextBox *title=0,*bgLabel=0,*status=0;
MyGUI::ScrollBar *rotation=0,*height=0,*horizontal=0,*zoom=0,*red=0,*green=0,*blue=0,*brightness=0,*opacity=0;
MyGUI::TextBox *redLabel=0,*greenLabel=0,*blueLabel=0,*brightnessLabel=0,*opacityLabel=0;
MyGUI::Button *resetButton=0,*cancelButton=0,*applyButton=0;
hand editing,pending;
bool portraitMenuRequested=false,extensionShown=false;
MyGUI::Button* popupAction=0;
Settings draft;
bool opened=false,updating=false,dirty=false,failed=false,resourcesReady=false;
struct WidgetLifetime : MyGUI::IUnlinkWidget {
 void _unlinkWidget(MyGUI::Widget* widget){
  if(widget==window){window=0;opened=false;editing.setNull();title=0;bgLabel=0;status=0;rotation=0;height=0;horizontal=0;zoom=0;red=0;green=0;blue=0;brightness=0;opacity=0;redLabel=0;greenLabel=0;blueLabel=0;brightnessLabel=0;opacityLabel=0;resetButton=0;cancelButton=0;applyButton=0;}
  if(widget==popup){popup=0;popupAction=0;portraitMenuRequested=false;extensionShown=false;}
  if(widget==preview){preview=0;opened=false;}
  if(widget==backdrop)backdrop=0;
 }
} widgetLifetime;
bool lifetimeRegistered=false;
void Resources(){if(!resourcesReady){Ogre::ResourceGroupManager::getSingleton().addResourceLocation(resources,"FileSystem","General");Ogre::ResourceGroupManager::getSingleton().addResourceLocation(backgroundFolder,"FileSystem","General");resourcesReady=true;}}
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
 char value[128];GetPrivateProfileStringA(k.c_str(),"view","0 0 1 0",value,128,config.c_str());
 Settings s;std::istringstream in(value);in>>s.yaw>>s.height>>s.zoom>>s.background;in>>s.r>>s.g>>s.b;in>>s.brightness>>s.opacity;in>>s.horizontal;s.horizontal=std::max(-1.f,std::min(1.f,s.horizontal));s.brightness=std::max(0.f,std::min(2.f,s.brightness));s.opacity=std::max(0.f,std::min(1.f,s.opacity));
 s.yaw=std::max(-180.f,std::min(180.f,s.yaw));s.height=std::max(-1.f,std::min(1.f,s.height));s.zoom=std::max(.6f,std::min(1.8f,s.zoom));s.background=std::max(0,std::min(5,s.background));
 if(!_finite(s.yaw) || !_finite(s.height) || !_finite(s.horizontal) || !_finite(s.zoom) || !_finite(s.r) || !_finite(s.g) || !_finite(s.b) || !_finite(s.brightness) || !_finite(s.opacity))s=Settings();
 s.r=std::max(0.f,std::min(1.f,s.r));s.g=std::max(0.f,std::min(1.f,s.g));s.b=std::max(0.f,std::min(1.f,s.b));
 char file[260];GetPrivateProfileStringA(k.c_str(),"background_file","",file,260,config.c_str());
 if(file[0]){s.background=0;for(size_t i=0;i<customBackgrounds.size();++i)if(customBackgrounds[i]==file){s.background=int(i)+6;break;}}
 settings[k]=s;return s;
}
Settings Effective(Character* c){return opened && editing.getCharacter()==c?draft:Get(c);}
void Store(Character* c,const Settings& s){std::string k=Key(c);if(k.empty())return;settings[k]=s;std::ostringstream out;out<<s.yaw<<' '<<s.height<<' '<<s.zoom<<' '<<s.background<<' '<<s.r<<' '<<s.g<<' '<<s.b<<' '<<s.brightness<<' '<<s.opacity<<' '<<s.horizontal;WritePrivateProfileStringA(k.c_str(),"view",out.str().c_str(),config.c_str());WritePrivateProfileStringA(k.c_str(),"background_file",s.background>=6?customBackgrounds[s.background-6].c_str():"",config.c_str());}
MyGUI::TextBox* Text(MyGUI::Widget* p,int x,int y,int w,int h,const std::string& caption){MyGUI::TextBox* t=p->createWidget<MyGUI::TextBox>("Kenshi_TextboxStandardText",MyGUI::IntCoord(x,y,w,h),MyGUI::Align::Default);t->setCaption(caption);t->setTextColour(MyGUI::Colour(.82f,.79f,.70f));t->setNeedMouseFocus(false);return t;}
void Background(){
 bgLabel->setCaption(draft.background<6?bgNames[draft.background]:customBackgrounds[draft.background-6]);backdrop->setVisible(false);
 bool colour=draft.background==4,chosen=draft.background!=0;
 red->setVisible(colour);green->setVisible(colour);blue->setVisible(colour);redLabel->setVisible(colour);greenLabel->setVisible(colour);blueLabel->setVisible(colour);
 brightness->setVisible(chosen);opacity->setVisible(chosen);brightnessLabel->setVisible(chosen);opacityLabel->setVisible(chosen);
 int row=colour?674:562;
 brightness->setPosition(140,row-3);brightnessLabel->setPosition(20,row);
 opacity->setPosition(140,row+37);opacityLabel->setPosition(20,row+40);
 int footer=colour?754:chosen?642:562;
 status->setPosition(20,footer);resetButton->setPosition(20,footer+30);cancelButton->setPosition(132,footer+30);applyButton->setPosition(244,footer+30);
 window->setSize(360,footer+70);
 MyGUI::IntSize view=MyGUI::RenderManager::getInstance().getViewSize();MyGUI::IntPoint position=window->getPosition();if(position.top+window->getHeight()>view.height)window->setPosition(position.left,std::max(0,view.height-window->getHeight()));
}
void Sync(){updating=true;rotation->setScrollPosition(size_t(draft.yaw+180));height->setScrollPosition(size_t((draft.height+1)*100));horizontal->setScrollPosition(size_t((draft.horizontal+1)*100));zoom->setScrollPosition(size_t((draft.zoom-.6f)*100));red->setScrollPosition(size_t(draft.r*255));green->setScrollPosition(size_t(draft.g*255));blue->setScrollPosition(size_t(draft.b*255));brightness->setScrollPosition(size_t(draft.brightness*100));opacity->setScrollPosition(size_t(draft.opacity*100));updating=false;Background();}
void Changed(){dirty=true;changedAt=GetTickCount();}
void Slider(MyGUI::ScrollBar* sender,size_t position){if(updating)return;if(sender==rotation)draft.yaw=float(position)-180;else if(sender==height)draft.height=float(position)/100-1;else if(sender==horizontal)draft.horizontal=float(position)/100-1;else if(sender==zoom)draft.zoom=.6f+float(position)/100;else if(sender==red)draft.r=float(position)/255;else if(sender==green)draft.g=float(position)/255;else if(sender==blue)draft.b=float(position)/255;else if(sender==brightness)draft.brightness=float(position)/100;else draft.opacity=float(position)/100;if(draft.background==4)Background();Changed();}
void BgNext(MyGUI::Widget*){draft.background=int((draft.background+1)%BackgroundCount());Background();Changed();}
void BgPrevious(MyGUI::Widget*){draft.background=int((draft.background+BackgroundCount()-1)%BackgroundCount());Background();Changed();}
void Reset(MyGUI::Widget*){draft=Settings();Sync();Changed();}
void Front(MyGUI::Widget*){draft.yaw=0;Sync();Changed();}
void ThreeQuarter(MyGUI::Widget*){draft.yaw=35;Sync();Changed();}
void QueuePortrait(){PortraitManager* pm=PortraitManager::getInstance();if(pm && editing.getCharacter()){PortraitData* pd=pm->getPortrait(editing);if(pd){pd->reloadPortrait=true;pd->forceUpdate();}}if(ou && editing.getCharacter())ou->addPortraitUpdate(editing);}
void Cancel(MyGUI::Widget*){opened=false;if(window)window->setVisible(false);QueuePortrait();}
void Apply(MyGUI::Widget*){Character* c=editing.getCharacter();if(c)Store(c,draft);opened=false;if(window)window->setVisible(false);QueuePortrait();}
MyGUI::Button* Button(MyGUI::Widget* p,int x,int y,int w,const char* label,void (*callback)(MyGUI::Widget*)){MyGUI::Button* b=p->createWidget<MyGUI::Button>("Kenshi_Button1",MyGUI::IntCoord(x,y,w,28),MyGUI::Align::Default);b->setCaption(label);b->eventMouseButtonClick+=MyGUI::newDelegate(callback);return b;}
MyGUI::ScrollBar* SliderRow(int y,const char* label,size_t range,MyGUI::TextBox** labelOut=0){MyGUI::TextBox* t=Text(window,20,y,110,25,label);if(labelOut)*labelOut=t;MyGUI::ScrollBar* s=window->createWidget<MyGUI::ScrollBar>("Kenshi_SliderHBig",MyGUI::IntCoord(140,y-3,200,34),MyGUI::Align::Default);MyGUI::Widget* start=s->findWidget("Start");MyGUI::Widget* end=s->findWidget("End");
 if(start){start->changeWidgetSkin("Kenshi_Button1");MyGUI::Button* b=start->castType<MyGUI::Button>(false);if(b)b->setCaption("<");}
 if(end){end->changeWidgetSkin("Kenshi_Button1");MyGUI::Button* b=end->castType<MyGUI::Button>(false);if(b)b->setCaption(">");}
 s->setScrollRange(range);s->setScrollPage(1);s->setMinTrackSize(36);s->setTrackSize(36);s->setMoveToClick(true);s->eventScrollChangePosition+=MyGUI::newDelegate(&Slider);return s;}
void OpenEditor(MyGUI::Widget*){
 portraitMenuRequested=false;extensionShown=false;if(ou)ou->hideContextMenu();if(opened)Cancel(0);editing=pending;Character* c=editing.getCharacter();if(!c)return;
 draft=Get(c);opened=true;popup->setVisible(false);window->setVisible(true);title->setCaption("PORTRAIT - "+c->getName());Sync();
 PortraitManager* pm=PortraitManager::getInstance();if(pm)pm->setImageWidget(editing,preview,true);Changed();
}
void Create(){
 if(!lifetimeRegistered){MyGUI::WidgetManager::getInstance().registerUnlinker(&widgetLifetime);lifetimeRegistered=true;}
 Resources();
 MyGUI::Gui* g=MyGUI::Gui::getInstancePtr();MyGUI::IntSize view=MyGUI::RenderManager::getInstance().getViewSize();
 window=g->createWidget<MyGUI::Widget>("Kenshi_GenericTextBoxSkin",MyGUI::IntCoord(std::max(0,(view.width-360)/2),std::max(0,(view.height-700)/2),360,700),MyGUI::Align::Default,"Window","PortraitEditorWindow");
 title=Text(window,15,10,290,28,"PORTRAIT");Button(window,315,8,28,"X",Cancel);
 MyGUI::Widget* square=window->createWidget<MyGUI::Widget>("PanelEmpty",MyGUI::IntCoord(52,48,256,256),MyGUI::Align::Default);
 backdrop=square->createWidget<MyGUI::ImageBox>("ImageBox",MyGUI::IntCoord(0,0,256,256),MyGUI::Align::Stretch);backdrop->setNeedMouseFocus(false);backdrop->setDepth(5);
 preview=square->createWidget<MyGUI::ImageBox>("ImageBox",MyGUI::IntCoord(0,0,256,256),MyGUI::Align::Stretch);preview->setNeedMouseFocus(false);preview->setDepth(3);
 rotation=SliderRow(325,"Rotation",361);height=SliderRow(365,"Camera height",201);horizontal=SliderRow(405,"Horizontal",201);zoom=SliderRow(445,"Zoom",121);
 Button(window,55,487,110,"Front",Front);Button(window,190,487,110,"3/4 view",ThreeQuarter);
 Text(window,20,527,110,24,"Background");Button(window,133,524,28,"<",BgPrevious);bgLabel=Text(window,168,527,140,24,"");Button(window,315,524,28,">",BgNext);
 red=SliderRow(562,"Red",256,&redLabel);green=SliderRow(598,"Green",256,&greenLabel);blue=SliderRow(634,"Blue",256,&blueLabel);
 brightness=SliderRow(674,"Brightness",201,&brightnessLabel);opacity=SliderRow(714,"Opacity",101,&opacityLabel);
 status=Text(window,20,635,325,22,"Preview updates when you release a control.");
 resetButton=Button(window,20,660,96,"Reset",Reset);cancelButton=Button(window,132,660,96,"Cancel",Cancel);applyButton=Button(window,244,660,96,"Apply",Apply);window->setVisible(false);
 popup=g->createWidget<MyGUI::Widget>("Kenshi_GenericTextBoxSkin",MyGUI::IntCoord(0,0,190,40),MyGUI::Align::Default,"Popup","PortraitEditorContext");popupAction=Button(popup,5,6,180,"Customize portrait",OpenEditor);popup->setVisible(false);
}
void (*originalNotify)(MainBarGUI*,MyGUI::ItemBox*,const MyGUI::IBNotifyItemData&)=0;
void Notify(MainBarGUI* bar,MyGUI::ItemBox* box,const MyGUI::IBNotifyItemData& info){
 // Leave the native portrait menu and its normal actions in control.
 originalNotify(bar,box,info);
 if(failed)return;
 if(info.id==MyGUI::MouseButton::Right && info.index<box->getItemCount()){
  PortraitData** pp=box->getItemDataAt<PortraitData*>(info.index,false);Character* c=pp && *pp?(*pp)->getCharacter():0;
  if(c && c->isPlayerCharacter()){pending=c->getHandle();portraitMenuRequested=true;extensionShown=false;}
 }
}
void (*originalMenuUpdate)(ContextMenu*)=0;
void MenuUpdate(ContextMenu* menu){
 originalMenuUpdate(menu);if(failed || !portraitMenuRequested || !menu){if(popup)popup->setVisible(false);return;}
 try{
  ContextMenuGUI* choices[]={menu->menuGUI,menu->menuGUI2};ContextMenuGUI* native=0;
  for(int i=0;i<2;++i)if(choices[i] && choices[i]->getVisible() && choices[i]->optionsList){native=choices[i];break;}
  if(!native || !pending.getCharacter()){if(popup)popup->setVisible(false);if(extensionShown){portraitMenuRequested=false;extensionShown=false;}return;}
  if(!window)Create();MyGUI::IntCoord rect=native->optionsList->getAbsoluteCoord();
  MyGUI::Widget* root=native->getMainWidget();if(root)rect=root->getAbsoluteCoord();
  int width=std::max(140,std::min(180,rect.width));
  popup->setCoord(rect.left+(rect.width-width)/2,std::max(0,rect.top-32),width,30);popupAction->setCoord(3,3,width-6,24);popup->setVisible(true);extensionShown=true;
 }catch(const std::exception& e){failed=true;if(popup)popup->setVisible(false);ErrorLog(std::string("Portrait Editor context menu: ")+e.what());}
}
void PollPortraitMenu(){
 if(failed || !portraitMenuRequested || !ou || !ou->player){if(popup)popup->setVisible(false);return;}
 try{
  ContextMenuGUI* choices[]={ou->player->contextMenu.menuGUI,ou->player->contextMenu.menuGUI2};ContextMenuGUI* native=0;
  for(int i=0;i<2;++i)if(choices[i] && choices[i]->getVisible()){native=choices[i];break;}
  if(!native || !pending.getCharacter()){if(popup)popup->setVisible(false);return;}
  if(!window)Create();MyGUI::Widget* root=native->getMainWidget();if(!root)return;MyGUI::IntCoord rect=root->getAbsoluteCoord();
  int width=std::max(140,std::min(180,rect.width));popup->setCoord(rect.left+(rect.width-width)/2,std::max(0,rect.top-32),width,30);popupAction->setCoord(3,3,width-6,24);popup->setVisible(true);extensionShown=true;
 }catch(const std::exception& e){ErrorLog(std::string("Portrait Editor menu placement: ")+e.what());if(popup)popup->setVisible(false);}
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
 originalCell(cell,info,data);if(failed || !data)return;Character* c=data->getCharacter();if(c && c->isPlayerCharacter())SetComposed(c,cell->imagePortrait);
}
void (*originalUpdate)(MainBarGUI*)=0;
void Update(MainBarGUI* bar){
 if(!bar)return;
 originalUpdate(bar);if(failed || !pluginReady)return;
 try{
  PollPortraitMenu();
  // Drain a snapshot. Rendering may queue more composition work during callbacks.
  std::map<std::string,hand> queued;queued.swap(pendingComposition);
  for(std::map<std::string,hand>::iterator i=queued.begin();i!=queued.end();++i){Character* c=i->second.getCharacter();if(c)ComposeRegistered(c);}
  if(!opened || !preview)return;
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
 originalLook(camera,target);
 if(active && !modifying && !active->adjusted){active->camera=camera;active->focus=target;active->hasFocus=true;}
}
void LookXYZ(Ogre::Camera* camera,float x,float y,float z){
 originalLookXYZ(camera,x,y,z);
 if(active && !modifying && !active->adjusted){active->camera=camera;active->focus=Ogre::Vector3(x,y,z);active->hasFocus=true;}
}
void Adjust(const Ogre::Camera* source){
 RenderContext* ctx=active;if(!pluginReady || !ctx || modifying || ctx->adjusted || !source)return;
 Ogre::Camera* camera=const_cast<Ogre::Camera*>(source);
 if(ctx->camera && ctx->camera!=camera)return;
 ctx->camera=camera;ctx->position=camera->getPosition();ctx->orientation=camera->getOrientation();ctx->fov=camera->getFOVy();ctx->adjusted=true;
 Ogre::Vector3 focus=ctx->hasFocus?ctx->focus:ctx->position+camera->getDirection()*10.f;
 Ogre::Quaternion turn(Ogre::Degree(ctx->settings.yaw),Ogre::Vector3::UNIT_Y);
 Ogre::Vector3 right=turn*(ctx->orientation*Ogre::Vector3::UNIT_X);
 Ogre::Vector3 offset=Ogre::Vector3(0,ctx->settings.height,0)+right*ctx->settings.horizontal;
 ModifyScope scope;
 camera->setPosition(focus+turn*(ctx->position-focus)+offset);
 originalLook(camera,focus+offset);
 camera->setFOVy(Ogre::Radian(ctx->fov.valueRadians()/ctx->settings.zoom));

}
const Ogre::Matrix4& View(const Ogre::Camera* camera){Adjust(camera);return originalView(camera);}
const Ogre::Matrix4& ViewOwn(const Ogre::Camera* camera,bool own){Adjust(camera);return originalViewOwn(camera,own);}
void Restore(RenderContext& ctx){if(ctx.adjusted && ctx.camera){ModifyScope scope;ctx.camera->setPosition(ctx.position);ctx.camera->setOrientation(ctx.orientation);ctx.camera->setFOVy(ctx.fov);}}
void Compose(Character* c,Ogre::SharedPtr<Ogre::Texture> atlas,const iVector2& tileSize,const Ogre::TRect<float>& rect){
 Settings s=Effective(c);if(s.background==0 || atlas.isNull())return;std::string key=Key(c);if(key.empty())return;
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
   Resources();std::string filename=customBackgrounds[s.background-6];std::ostringstream cacheName;cacheName<<filename<<':'<<tw<<'x'<<th;std::vector<unsigned char>& cached=backgroundPixels[cacheName.str()];
   if(cached.empty()){
    Ogre::Image image;image.load(filename,"General");size_t iw=image.getWidth(),ih=image.getHeight();if(!iw || !ih || iw>2048 || ih>2048){ErrorLog("Portrait Editor: background must be 2048x2048 or smaller");return;}
    size_t side=std::min(iw,ih),ox=(iw-side)/2,oy=(ih-side)/2;cached.resize(tw*th*3);
    for(size_t y=0;y<th;++y)for(size_t x=0;x<tw;++x){Ogre::ColourValue colour=image.getColourAt(ox+x*side/tw,oy+y*side/th,0);size_t n=(y*tw+x)*3;cached[n]=static_cast<unsigned char>(colour.r*255);cached[n+1]=static_cast<unsigned char>(colour.g*255);cached[n+2]=static_cast<unsigned char>(colour.b*255);}
   }
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
 if(!pluginReady || !c || !c->isPlayerCharacter()){originalRender(machine,c,texture,size,rect);return;}
 hand saved=c->getHandle();
 RenderContext ctx;ctx.character=c;ctx.settings=Effective(c);RenderContext* previous=active;active=&ctx;
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
 KenshiLib::BinaryVersion version=KenshiLib::GetKenshiVersion();if(version.GetPlatform()!=KenshiLib::BinaryVersion::STEAM || version.GetVersion()!="1.0.65"){ErrorLog("Portrait Editor: unsupported executable; hooks disabled");return;}
 HMODULE module=0;GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(&startPlugin),&module);char path[MAX_PATH];GetModuleFileNameA(module,path,MAX_PATH);resources=path;resources=resources.substr(0,resources.find_last_of("\\/"))+"/gui";
 backgroundFolder=resources.substr(0,resources.find_last_of("\\/"))+"/backgrounds";ScanBackgrounds();
 char local[MAX_PATH];DWORD length=GetEnvironmentVariableA("LOCALAPPDATA",local,MAX_PATH);if(!length || length>=MAX_PATH){ErrorLog("Portrait Editor: cannot find settings folder");return;}std::string folder=std::string(local)+"/kenshi";CreateDirectoryA(folder.c_str(),0);config=folder+"/portrait_editor.ini";
 HMODULE ogre=GetModuleHandleA("OgreMain_x64.dll");
 const char* exports[]={"?lookAt@Camera@Ogre@@QEAAXAEBVVector3@2@@Z","?lookAt@Camera@Ogre@@QEAAXMMM@Z","?getViewMatrix@Camera@Ogre@@UEBAAEBVMatrix4@2@XZ","?getViewMatrix@Camera@Ogre@@QEBAAEBVMatrix4@2@_N@Z"};
 void* targets[4];for(int i=0;i<4;++i){targets[i]=ogre?reinterpret_cast<void*>(GetProcAddress(ogre,exports[i])):0;if(!targets[i]){ErrorLog("Portrait Editor: camera entry unavailable");return;}}
 if(KenshiLib::AddHook(targets[0],&Look,&originalLook)!=KenshiLib::SUCCESS)failed=true;
 if(KenshiLib::AddHook(targets[1],&LookXYZ,&originalLookXYZ)!=KenshiLib::SUCCESS)failed=true;
 if(KenshiLib::AddHook(targets[2],&View,&originalView)!=KenshiLib::SUCCESS)failed=true;
 if(KenshiLib::AddHook(targets[3],&ViewOwn,&originalViewOwn)!=KenshiLib::SUCCESS)failed=true;
 HMODULE gui=GetModuleHandleA("MyGUIEngine_x64.dll");void* uvTarget=gui?reinterpret_cast<void*>(GetProcAddress(gui,"?_setUVSet@ImageBox@MyGUI@@AEAAXAEBU?$TRect@M@types@2@@Z")):0;
 void* textureTarget=gui?reinterpret_cast<void*>(GetProcAddress(gui,"?setImageTexture@ImageBox@MyGUI@@QEAAXAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z")):0;
 if(!textureTarget || KenshiLib::AddHook(textureTarget,&Texture,&originalTexture)!=KenshiLib::SUCCESS)failed=true;
 if(!uvTarget || KenshiLib::AddHook(uvTarget,&UV,&originalUV)!=KenshiLib::SUCCESS)failed=true;
 if(KenshiLib::AddHook(KenshiLib::GetRealAddress(&PortraitManager::setImageWidget),&SetImage,&originalSetImage)!=KenshiLib::SUCCESS)failed=true;
 if(KenshiLib::AddHook(KenshiLib::GetRealAddress(&RenderToTextureotron::renderCharacter),&Render,&originalRender)!=KenshiLib::SUCCESS)failed=true;

 if(KenshiLib::AddHook(KenshiLib::GetRealAddress(&MainBarGUI::portraitNotifyItem),&Notify,&originalNotify)!=KenshiLib::SUCCESS)failed=true;
 if(KenshiLib::AddHook(KenshiLib::GetRealAddress(&PortraitMainCellView::update),&Cell,&originalCell)!=KenshiLib::SUCCESS)failed=true;
 if(KenshiLib::AddHook(KenshiLib::GetRealAddress(&MainBarGUI::_NV_update),&Update,&originalUpdate)!=KenshiLib::SUCCESS)failed=true;
 if(failed){failed=true;ErrorLog("Portrait Editor: hooks could not be installed");}else {pluginReady=true;DebugLog("Portrait Editor loaded: right-click a player portrait to customize");}
}


