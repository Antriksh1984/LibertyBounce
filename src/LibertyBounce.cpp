// LibertyBounce - GTA III (PC v1.0) port of ViceBounce by tek-95 (MIT).
// Squashes and stretches vehicles and people to the beat of your own music.
//
// How it works: the game copies each entity's CMatrix into the root RwFrame of
// its model before drawing. Right before CEntity::Render (0x474BD0) we replace
// that root frame matrix with a squashed copy; children (wheels, doors, ped
// limbs, held weapons) inherit it through the frame hierarchy. Physics and
// collision never see it: every frame is restored at the 3D/2D boundary
// (Render2dStuff) and again before CGame::Process.
//
// Cutscene actors are CCutsceneObjects whose root frame sits at the scene
// origin, so they are squashed around the centre of their own body parts
// instead, all at once before RenderScene. CCutsceneHead::Render rebuilds each
// head from its body's head bone (RwFrameGetLTM), so heads follow the squash.
#include "plugin.h"
#include "CVehicle.h"
#include "CPed.h"
#include "CModelInfo.h"
#include "CColModel.h"
#include "CTimer.h"
#include "CReplay.h"
#include "CCutsceneMgr.h"
#include "CCutsceneObject.h"
#include "eVehicleType.h"
#include "BounceMath.h"
#include "BeatMap.h"
#include "MusicPlayback.h"
#include "MusicImporter.h"
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;
namespace {
using namespace libertybounce;
constexpr const char* section="LibertyBounce";
Settings cfg;
double phase=0;
std::string iniPath,logPath;
MusicPlayback music;
std::wstring musicFile,musicDirectory,beatMapFile;
BeatMap beatMap;
MusicImporter importer;
bool importRequested=false,useBeatMap=true;
double musicSyncOffsetMs=0,firstBeatMs=0;
bool musicEnabled=true,musicPending=false,musicErrorLogged=false,musicStartedLogged=false;
// Radio following. Station maps are indexed like cMusicManager's streamed
// sounds: 0 HEAD ... 8 CHAT (9 is the MP3 player, which can't be pre-analysed).
constexpr const char* stationNames[]={"HEAD","CLASS","KJAH","RISE","LIPS","GAME","MSX","FLASH","CHAT"};
std::array<BeatMap,std::size(stationNames)> stationMaps;
MusicImporter radioImporter;
bool radioEnabled=true;
double radioSyncOffsetMs=0;
int radioStation=-1;
// How beat-driven the audio is right now (0..1); scales the bounce so radio
// talk and adverts fade it out. Always 1 for your own song.
double beatStrength=1;

// Hotkeys are virtual-key codes from the INI; defaults match ViceBounce, plus
// F12 for custom music on/off. Radio following has no key unless one is set
// (KeyRadio), so it can't be switched off by accident.
struct Keys { int cars=VK_F6,reload=VK_F7,other=VK_F8,align=VK_F9,people=VK_F10,import=VK_F11,bpmUp=VK_PRIOR,bpmDown=VK_NEXT,ampUp=VK_HOME,ampDown=VK_END,customMusic=VK_F12,radio=0; } keys;

void Log(const std::string& text) {
    std::ofstream out(logPath,std::ios::app);
    out << GetTickCount64() << " " << text << '\n';
}
void Status(const char* action) {
    char value[160];
    std::snprintf(value,sizeof(value),"%s cars=%d other=%d people=%d bpm=%.0f amplitude=%.3f easing=%d",action,cfg.enabled,cfg.otherVehicles,cfg.allPeople,cfg.bpm,cfg.amplitude,cfg.easing);
    Log(value);
}

bool Paused() {
    // CReplay::Mode 1 = playback.
    return VisualPaused(CTimer::m_UserPause,CTimer::m_CodePause,CReplay::Mode==1,CCutsceneMgr::ms_running);
}
bool EffectActive(CVehicle* vehicle) {
    return vehicle&&VehicleEnabled(cfg,int(vehicle->m_nVehicleClass))&&cfg.amplitude>0&&!Paused();
}
Scale ActiveScale() { auto visual=cfg;visual.enabled=true;visual.amplitude*=beatStrength;return GetScale(phase,visual); }

// ---- config -------------------------------------------------------------
double ReadNumber(const char* name,const char* fallback,double lo,double hi) {
    char value[96];
    GetPrivateProfileStringA(section,name,fallback,value,sizeof(value),iniPath.c_str());
    return ParseNumber(value,std::strtod(fallback,nullptr),lo,hi);
}
int ReadKey(const char* name,int fallback) {
    char value[32];
    GetPrivateProfileStringA(section,name,"",value,sizeof(value),iniPath.c_str());
    char* end=nullptr;
    const long key=std::strtol(value,&end,0); // accepts 112 or 0x70
    return end!=value&&key>0&&key<256?int(key):fallback;
}
void LoadStationMaps();
std::wstring BesideIni(const std::wstring& folder,const std::wstring& path) {
    if(!path.empty()&&path.find(L':')==std::wstring::npos&&path[0]!=L'\\'&&path[0]!=L'/') return folder+path;
    return path;
}
void LoadConfig(bool requestImport=true) {
    cfg.bpm=ReadNumber("BPM","135",20,300);
    cfg.amplitude=ReadNumber("Amplitude","0.20",0,0.75);
    cfg.easing=int(ReadNumber("Easing","1",0,3));
    cfg.enabled=ReadNumber("Enabled","1",0,1)>=.5;
    cfg.otherVehicles=ReadNumber("OtherVehicles","1",0,1)>=.5;
    cfg.allPeople=ReadNumber("AllPeople","0",0,1)>=.5;
    musicEnabled=ReadNumber("MusicEnabled","1",0,1)>=.5;
    firstBeatMs=ReadNumber("MusicFirstBeatMs","0",-3600000,3600000);
    useBeatMap=ReadNumber("MusicUseBeatMap","1",0,1)>=.5;
    musicSyncOffsetMs=ReadNumber("MusicSyncOffsetMs","0",-3600000,3600000);
    const bool autoImport=ReadNumber("MusicAutoImport","1",0,1)>=.5;
    Keys defaults;
    keys.cars=ReadKey("KeyCars",defaults.cars);
    keys.reload=ReadKey("KeyReload",defaults.reload);
    keys.other=ReadKey("KeyOtherVehicles",defaults.other);
    keys.people=ReadKey("KeyAllPeople",defaults.people);
    keys.align=ReadKey("KeyAlignBeat",defaults.align);
    keys.import=ReadKey("KeyImportMusic",defaults.import);
    keys.bpmUp=ReadKey("KeyBpmUp",defaults.bpmUp);
    keys.bpmDown=ReadKey("KeyBpmDown",defaults.bpmDown);
    keys.ampUp=ReadKey("KeyAmplitudeUp",defaults.ampUp);
    keys.ampDown=ReadKey("KeyAmplitudeDown",defaults.ampDown);
    keys.customMusic=ReadKey("KeyCustomMusic",defaults.customMusic);
    keys.radio=ReadKey("KeyRadio",defaults.radio);
    radioEnabled=ReadNumber("Radio","1",0,1)>=.5;
    radioSyncOffsetMs=ReadNumber("RadioSyncOffsetMs","0",-60000,60000);

    wchar_t module[MAX_PATH]{},path[1024]{};
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase),module,MAX_PATH);
    std::wstring folder(module);folder=folder.substr(0,folder.find_last_of(L"\\/")+1);
    musicDirectory=folder;
    const auto wideIni=folder+L"LibertyBounce.ini";
    GetPrivateProfileStringW(L"LibertyBounce",L"MusicFile",L"",path,1024,wideIni.c_str());
    musicFile=BesideIni(folder,path);
    GetPrivateProfileStringW(L"LibertyBounce",L"MusicBeatMapFile",L"",path,1024,wideIni.c_str());
    beatMapFile=BesideIni(folder,path);
    GetPrivateProfileStringW(L"LibertyBounce",L"MusicSourceFile",L"",path,1024,wideIni.c_str());
    const auto source=BesideIni(folder,path);
    importer.Configure(source,autoImport);
    importRequested=requestImport&&musicEnabled&&autoImport&&!source.empty();
    musicPending=true;musicErrorLogged=false;musicStartedLogged=false;
    LoadStationMaps();
    Status("config loaded");
}
void SaveValue(const char* name,double value) {
    char text[40];std::snprintf(text,sizeof(text),"%.4f",value);
    WritePrivateProfileStringA(section,name,text,iniPath.c_str());
}
bool Focused() {
    DWORD process=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&process);
    return process==GetCurrentProcessId();
}

// ---- in-game radio -------------------------------------------------------
// gta3.exe v1.0: the MusicManager and SampleManager instances, and
// cSampleManager::GetStreamedFilePosition(stream), which returns the ms
// position inside the streamed station file (0 when nothing is streaming).
constexpr uintptr_t musicManagerAddress=0x8F3964,sampleManagerAddress=0x7341E0,getStreamedFilePositionAddress=0x568130;

void LoadStationMaps() {
    int loaded=0;
    for(size_t i=0;i<stationMaps.size();++i) {
        stationMaps[i]=BeatMap{};
        std::ifstream input{std::filesystem::path(musicDirectory)/"LibertyBounceRadio"/(std::string(stationNames[i])+".txt")};
        if(input&&stationMaps[i].Load(input)) ++loaded;
    }
    Log("radio beat maps loaded: "+std::to_string(loaded)+" of "+std::to_string(stationMaps.size())+" stations");
}
// cMusicManager::m_nCurrentStreamedSound: what the game streams on stream 0
// (StartStreamedFile is always called with this byte). 0-8 are the radio
// stations; ambience, police radio, mission and cutscene audio are higher.
int StreamedSound() { return reinterpret_cast<const unsigned char*>(musicManagerAddress)[3]; }
// Station whose beat map drives the bounce right now, or -1.
int CurrentStation(double& position) {
    if(!radioEnabled) return -1;
    const int station=StreamedSound();
    if(station<0||station>=int(stationMaps.size())||!stationMaps[station].Valid()) return -1;
    using GetPositionFn=unsigned(__thiscall*)(void*,unsigned char);
    const unsigned ms=reinterpret_cast<GetPositionFn>(getStreamedFilePositionAddress)(reinterpret_cast<void*>(sampleManagerAddress),0);
    if(!ms) return -1;
    position=double(ms);
    return station;
}
void StartRadioAnalysis() {
    // The game's own audio folder (gta3.exe's folder + "audio").
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr,exe,MAX_PATH);
    std::wstring audio(exe);audio=audio.substr(0,audio.find_last_of(L"\\/")+1)+L"audio";
    if(radioImporter.StartRadio(musicDirectory,audio)) Log("checking radio station beat maps in background");
    else Log("radio analysis unavailable (LibertyBounceMusic.exe missing?)");
}

// ---- frame deformation -------------------------------------------------
struct SavedFrame { RwFrame* frame; RwMatrix original; };
std::vector<SavedFrame> savedFrames;

void RestoreFrames() {
    for(const auto& saved:savedFrames) {
        *RwFrameGetMatrix(saved.frame)=saved.original;
        RwFrameUpdateObjects(saved.frame);RwFrameGetLTM(saved.frame);
    }
    savedFrames.clear();
}
RwFrame* RootFrame(CEntity* entity) {
    auto* object=entity->m_pRwObject;
    if(!object||!RenderableObjectType(object->type)) return nullptr;
    return static_cast<RwFrame*>(object->parent);
}
// Lowest point of the model in its own space (wheels for cars, feet for peds).
float ModelBottom(CEntity* entity,float fallback) {
    if(entity->m_nModelIndex<0) return fallback;
    auto* model=CModelInfo::GetModelInfo(entity->m_nModelIndex);
    if(!model||!model->m_pColModel) return fallback;
    const float bottom=model->m_pColModel->m_boundBox.m_vecMin.z;
    return std::isfinite(bottom)&&bottom<0&&bottom>-20?bottom:fallback;
}
// Squashing around the pivot lifts the bottom off the ground; shift back down
// along the entity's own up axis so wheels and feet stay planted.
CVector GroundShift(CEntity* entity,float bottom) {
    const float along=bottom*(1-ActiveScale().vertical);
    const auto& up=entity->m_matrix.at;
    return CVector(up.x*along,up.y*along,up.z*along);
}
CVector VehicleShift(CVehicle* vehicle) {
    // Boats keep their waterline and aircraft their altitude.
    const auto kind=vehicle->m_nVehicleClass;
    if(kind!=VEHICLE_AUTOMOBILE&&kind!=VEHICLE_TRAIN) return CVector(0,0,0);
    return GroundShift(vehicle,ModelBottom(vehicle,-0.5f));
}
template<class Basis> void Deform(RwFrame* frame,const Basis& basis,const CVector& shift) {
    for(const auto& saved:savedFrames) if(saved.frame==frame) return;
    savedFrames.push_back({frame,*RwFrameGetMatrix(frame)});
    RwMatrix matrix=DeformedMatrix(savedFrames.back().original,basis,ActiveScale());
    matrix.pos.x+=shift.x;matrix.pos.y+=shift.y;matrix.pos.z+=shift.z;
    *RwFrameGetMatrix(frame)=matrix;
    RwFrameUpdateObjects(frame);RwFrameGetLTM(frame);
}

void BeforeVehicleRender(CVehicle* vehicle) {
    if(!EffectActive(vehicle)) return;
    auto* frame=RootFrame(vehicle);
    if(!frame) return;
    Deform(frame,vehicle->m_matrix,VehicleShift(vehicle));
    static bool logged=false;
    if(!logged){Log("first vehicle rendered with bounce");logged=true;}
}
void BeforePedRender(CPed* ped) {
    if(!ped||cfg.amplitude<=0||Paused()) return;
    auto* frame=RootFrame(ped);
    if(!frame) return;
    // Occupants squash with their vehicle, around the vehicle's axes.
    auto* vehicle=ped->m_bInVehicle?ped->m_pVehicle:nullptr;
    if(EffectActive(vehicle)) Deform(frame,vehicle->m_matrix,VehicleShift(vehicle));
    else if(cfg.allPeople) Deform(frame,ped->m_matrix,GroundShift(ped,ModelBottom(ped,-1.0f)));
    else return;
    static bool logged=false;
    if(!logged){Log("first person rendered with bounce");logged=true;}
}

// ---- cutscene actors -----------------------------------------------------
// gta3.exe v1.0 CCutsceneHead vtable (slot 13 = Render 0x4BA800).
constexpr uintptr_t cutsceneHeadVtable=0x5F7C08;

struct ActorBounds {
    RwV3d min{INFINITY,INFINITY,INFINITY},max{-INFINITY,-INFINITY,-INFINITY};
    void Add(const RwV3d& p) {
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)) return;
        min.x=std::min(min.x,p.x);min.y=std::min(min.y,p.y);min.z=std::min(min.z,p.z);
        max.x=std::max(max.x,p.x);max.y=std::max(max.y,p.y);max.z=std::max(max.z,p.z);
    }
    bool Valid() const {return std::isfinite(min.x);}
    RwV3d Center() const {return {(min.x+max.x)*.5f,(min.y+max.y)*.5f,(min.z+max.z)*.5f};}
};
RpAtomic* AddAtomicPosition(RpAtomic* atomic,void* bounds) {
    static_cast<ActorBounds*>(bounds)->Add(RwFrameGetLTM(RpAtomicGetFrame(atomic))->pos);
    return atomic;
}
void DeformCutsceneActor(CCutsceneObject* actor) {
    // Heads copy their body's (already squashed) head bone when they render.
    if(*reinterpret_cast<const uintptr_t*>(actor)==cutsceneHeadVtable) return;
    auto* frame=RootFrame(actor);
    if(!frame) return;
    auto* model=actor->m_nModelIndex>=0?CModelInfo::GetModelInfo(actor->m_nModelIndex):nullptr;
    const int type=model?int(model->m_nType):-1;
    const bool humanoid=type==MODEL_INFO_PED;
    // Same rule as ViceBounce: F6 covers everything in a cutscene, F10 people.
    if(!cfg.enabled&&!(humanoid&&cfg.allPeople)) return;
    // Body parts and prop pieces are atomics on animated child frames; the
    // clump's root frame stays at the scene origin and is left out.
    ActorBounds bounds;
    if(actor->m_pRwObject->type==rpCLUMP) RpClumpForAllAtomics(actor->m_pRwClump,AddAtomicPosition,&bounds);
    else bounds.Add(RwFrameGetLTM(frame)->pos);
    if(!bounds.Valid()) return;
    const auto pivot=bounds.Center();
    const auto basis=UprightActorBasis(*RwFrameGetLTM(frame),pivot);
    // People and cars keep their lowest part on the floor; props keep their pivot.
    float drop=0;
    if(humanoid||type==MODEL_INFO_VEHICLE) drop=(bounds.min.z-pivot.z)*(1-ActiveScale().vertical);
    Deform(frame,basis,CVector(0,0,drop));
    static bool logged=false;
    if(!logged){Log("first cutscene actor rendered with bounce");logged=true;}
}
void BeforeRenderScene() {
    if(!CCutsceneMgr::ms_running||cfg.amplitude<=0||Paused()) return;
    const unsigned count=std::min(CCutsceneMgr::ms_numCutsceneObjs,50u);
    for(unsigned i=0;i<count;++i)
        if(auto* actor=CCutsceneMgr::ms_pCutsceneObjects[i]) DeformCutsceneActor(actor);
}

// ---- per-frame tick at the 3D/2D boundary -------------------------------
void Tick() {
    RestoreFrames();
    static ULONGLONG previous=GetTickCount64();
    const auto now=GetTickCount64();
    const double elapsed=(now-previous)/1000.0;previous=now;
    const bool active=Focused()&&!Paused();
    // Open audio lazily on the game thread, never from DllMain.
    const int importResult=importer.Poll();
    if(importResult==0) {LoadConfig(false);Log("music import completed; reloading track profile");}
    else if(importResult>0&&importResult!=2) Log("music import failed; see LibertyBounceImportStatus.json");
    if(importRequested) {
        importRequested=false;
        if(importer.Start(musicDirectory)) {music.Configure(false,L"");Log("checking selected music file in background");}
        else Log("automatic music import unavailable; existing track profile kept");
    }
    if(musicEnabled&&importer.Changed()&&importer.Start(musicDirectory)) {
        music.Configure(false,L"");musicPending=true;Log("source music changed; rebuilding track profile");
    }
    if(musicPending&&!importer.Running()) {
        music.Configure(musicEnabled,musicFile);musicPending=false;beatMap=BeatMap{};
        if(useBeatMap&&!beatMapFile.empty()) {
            std::ifstream input{std::filesystem::path(beatMapFile)};
            if(beatMap.Load(input)) Log("loaded beat map: "+std::to_string(beatMap.Size())+" beats");
            else Log("beat map invalid/missing; using BPM and first beat instead");
        }
    }
    const int radioResult=radioImporter.Poll();
    if(radioResult==0) {Log("radio analysis completed");LoadStationMaps();}
    else if(radioResult>0) Log("radio analysis failed; see LibertyBounceRadioStatus.json");

    // A playing car radio station takes over from your own song.
    static int lastStreamed=-1;
    if(StreamedSound()!=lastStreamed) {
        lastStreamed=StreamedSound();
        Log("game now streams sound "+std::to_string(lastStreamed)+(lastStreamed<int(stationMaps.size())?std::string(" (radio station ")+stationNames[lastStreamed]+")":std::string(" (not a radio station)")));
    }
    double radioPosition=0;
    const int station=CurrentStation(radioPosition);
    if(station!=radioStation) {
        Log(station>=0?std::string("radio station ")+stationNames[station]+" drives the bounce":std::string("radio not followed; own music/BPM drives the bounce"));
        radioStation=station;
    }
    music.SetPaused(station>=0);
    double musicPosition=0,targetStrength=1;
    const bool synced=station<0&&music.Position(musicPosition);
    if(station>=0) {
        const auto& map=stationMaps[station];
        phase=map.Phase(radioPosition-radioSyncOffsetMs);
        targetStrength=map.Strength(radioPosition-radioSyncOffsetMs);
    } else if(synced) {
        phase=useBeatMap&&beatMap.Valid()?beatMap.Phase(musicPosition-musicSyncOffsetMs):MusicPhase(musicPosition,firstBeatMs,cfg.bpm);
        if(!musicStartedLogged){Log("music playback active; track position drives the bounce");musicStartedLogged=true;}
    } else if(!musicEnabled) {
        targetStrength=0; // custom music off and no radio: nothing to bounce to
    } else if(music.Error()&&!musicErrorLogged) {
        Log("music unavailable MCI error="+std::to_string(music.Error())+"; bouncing at INI BPM instead");musicErrorLogged=true;
    }
    // Ease towards the target over ~0.25 s so talk/music changes never pop.
    beatStrength+=(targetStrength-beatStrength)*std::min(1.0,std::max(0.0,elapsed)*4);

    const int watched[]={keys.cars,keys.other,keys.people,keys.align,keys.import,keys.bpmUp,keys.bpmDown,keys.ampUp,keys.ampDown,keys.reload,keys.radio,keys.customMusic};
    constexpr size_t count=std::size(watched);
    static std::array<bool,count> wasDown{};
    std::array<bool,count> pressed{};
    for(size_t i=0;i<count;++i) {
        const bool down=(GetAsyncKeyState(watched[i])&0x8000)!=0;
        pressed[i]=active&&down&&!wasDown[i];wasDown[i]=down;
    }
    if(!active) return;
    if(station<0&&!synced&&elapsed<=0.25) phase=AdvancePhase(phase,elapsed,cfg.bpm);
    // Ctrl+Shift+F11 belongs to OBS; only the bare import key opens the picker.
    const bool modifier=(GetAsyncKeyState(VK_SHIFT)&0x8000)||(GetAsyncKeyState(VK_CONTROL)&0x8000)||(GetAsyncKeyState(VK_MENU)&0x8000);
    if(pressed[0]){cfg.enabled=!cfg.enabled;SaveValue("Enabled",cfg.enabled);Status("cars toggle");}
    if(pressed[1]){cfg.otherVehicles=!cfg.otherVehicles;SaveValue("OtherVehicles",cfg.otherVehicles);Status("boats/trains/aircraft toggle");}
    if(pressed[2]){cfg.allPeople=!cfg.allPeople;SaveValue("AllPeople",cfg.allPeople);Status("all people toggle");}
    if(pressed[3]&&station>=0){
        // One offset for all stations: it corrects audio output latency.
        const auto& map=stationMaps[station];
        radioSyncOffsetMs=radioPosition-map.Nearest(radioPosition-radioSyncOffsetMs);
        SaveValue("RadioSyncOffsetMs",radioSyncOffsetMs);
        Status("current radio position marked as squash beat");
    } else if(pressed[3]){
        phase=.5;
        if(synced){
            if(useBeatMap&&beatMap.Valid()) {
                musicSyncOffsetMs=musicPosition-beatMap.Nearest(musicPosition-musicSyncOffsetMs);
                SaveValue("MusicSyncOffsetMs",musicSyncOffsetMs);
            } else {firstBeatMs=musicPosition;SaveValue("MusicFirstBeatMs",firstBeatMs);}
        }
        Status(synced?"current track position marked as squash beat":"squash beat phase reset");
    }
    if(pressed[9]) LoadConfig();
    if(pressed[4]&&!modifier) {
        if(importer.Start(musicDirectory,true)) Log("music file picker opened");
        else Log("music picker unavailable (LibertyBounceMusic.exe missing?) or import already running");
    }
    if(pressed[5]||pressed[6]){
        cfg.bpm=std::clamp(cfg.bpm+(pressed[5]?5:-5),20.0,300.0);
        useBeatMap=false;SaveValue("MusicUseBeatMap",0);SaveValue("BPM",cfg.bpm);Status("manual tempo changed; beat map disabled");
    }
    if(pressed[7]||pressed[8]){cfg.amplitude=std::clamp(cfg.amplitude+(pressed[7]?0.025:-0.025),0.0,0.75);SaveValue("Amplitude",cfg.amplitude);Status("amplitude changed");}
    if(pressed[10]){radioEnabled=!radioEnabled;SaveValue("Radio",radioEnabled);Log(radioEnabled?"radio following on":"radio following off");}
    if(pressed[11]){
        musicEnabled=!musicEnabled;SaveValue("MusicEnabled",musicEnabled);
        if(musicEnabled) musicPending=true; // reopened above on the next frame
        else music.Configure(false,L"");    // closes the track, so it stops
        Log(musicEnabled?"custom music on":"custom music off; only the radio drives the bounce");
    }
}

struct LibertyBounce {
    LibertyBounce() {
        char module[MAX_PATH]{};
        GetModuleFileNameA(reinterpret_cast<HMODULE>(&__ImageBase),module,MAX_PATH);
        std::string dir(module);dir=dir.substr(0,dir.find_last_of("\\/")+1);
        iniPath=dir+"LibertyBounce.ini";logPath=dir+"LibertyBounce.log";
        Log(std::string("LibertyBounce 0.3.0 startup; ")+plugin::GetGameVersionName());
        // Every address used (SDK events, CReplay::Mode, CCutsceneMgr::ms_running) is v1.0.
        if(plugin::GetGameVersion()!=GAME_10EN) {Log("unsupported executable: LibertyBounce needs gta3.exe v1.0; disabled");return;}
        LoadConfig();
        // Cheap when nothing changed: the importer skips stations it has already analysed.
        if(radioEnabled) StartRadioAnalysis();
        savedFrames.reserve(256);
        plugin::Events::vehicleRenderEvent.before+=BeforeVehicleRender;
        plugin::Events::pedRenderEvent.before+=BeforePedRender;
        plugin::Events::renderSceneEvent.before+=BeforeRenderScene;
        plugin::Events::drawingEvent.before+=Tick;
        plugin::Events::gameProcessEvent.before+=RestoreFrames;
        // Never leave a borrowed frame pointer alive across entity destruction.
        plugin::Events::vehicleDtorEvent.before+=[](CVehicle*){RestoreFrames();};
        plugin::Events::pedDtorEvent.before+=[](CPed*){RestoreFrames();};
        Log("render hooks installed for cars, boats, trains, helicopters, planes, people and cutscene actors");
    }
} instance;
}
