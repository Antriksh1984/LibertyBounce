// LibertyBounce - GTA III (PC v1.0) port of ViceBounce by tek-95 (MIT).
// Squashes and stretches vehicles and people to the beat of your own music.
//
// How it works: the game copies each entity's CMatrix into the root RwFrame of
// its model before drawing. Right before CEntity::Render (0x474BD0) we replace
// that root frame matrix with a squashed copy; children (wheels, doors, ped
// limbs, held weapons) inherit it through the frame hierarchy. Physics and
// collision never see it: every frame is restored at the 3D/2D boundary
// (Render2dStuff) and again before CGame::Process.
#include "plugin.h"
#include "CVehicle.h"
#include "CPed.h"
#include "CModelInfo.h"
#include "CColModel.h"
#include "CTimer.h"
#include "CReplay.h"
#include "CCutsceneMgr.h"
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

// Hotkeys are virtual-key codes from the INI; defaults match ViceBounce.
struct Keys { int cars=VK_F6,reload=VK_F7,other=VK_F8,align=VK_F9,people=VK_F10,import=VK_F11,bpmUp=VK_PRIOR,bpmDown=VK_NEXT,ampUp=VK_HOME,ampDown=VK_END; } keys;

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
Scale ActiveScale() { auto visual=cfg;visual.enabled=true;return GetScale(phase,visual); }

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
void Deform(RwFrame* frame,const CMatrix& basis,const CVector& shift) {
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
    double musicPosition=0;
    const bool synced=music.Position(musicPosition);
    if(synced) {
        phase=useBeatMap&&beatMap.Valid()?beatMap.Phase(musicPosition-musicSyncOffsetMs):MusicPhase(musicPosition,firstBeatMs,cfg.bpm);
        if(!musicStartedLogged){Log("music playback active; track position drives the bounce");musicStartedLogged=true;}
    } else if(musicEnabled&&music.Error()&&!musicErrorLogged) {
        Log("music unavailable MCI error="+std::to_string(music.Error())+"; bouncing at INI BPM instead");musicErrorLogged=true;
    }

    const int watched[]={keys.cars,keys.other,keys.people,keys.align,keys.import,keys.bpmUp,keys.bpmDown,keys.ampUp,keys.ampDown,keys.reload};
    constexpr size_t count=std::size(watched);
    static std::array<bool,count> wasDown{};
    std::array<bool,count> pressed{};
    for(size_t i=0;i<count;++i) {
        const bool down=(GetAsyncKeyState(watched[i])&0x8000)!=0;
        pressed[i]=active&&down&&!wasDown[i];wasDown[i]=down;
    }
    if(!active) return;
    if(!synced&&elapsed<=0.25) phase=AdvancePhase(phase,elapsed,cfg.bpm);
    // Ctrl+Shift+F11 belongs to OBS; only the bare import key opens the picker.
    const bool modifier=(GetAsyncKeyState(VK_SHIFT)&0x8000)||(GetAsyncKeyState(VK_CONTROL)&0x8000)||(GetAsyncKeyState(VK_MENU)&0x8000);
    if(pressed[0]){cfg.enabled=!cfg.enabled;SaveValue("Enabled",cfg.enabled);Status("cars toggle");}
    if(pressed[1]){cfg.otherVehicles=!cfg.otherVehicles;SaveValue("OtherVehicles",cfg.otherVehicles);Status("boats/trains/aircraft toggle");}
    if(pressed[2]){cfg.allPeople=!cfg.allPeople;SaveValue("AllPeople",cfg.allPeople);Status("all people toggle");}
    if(pressed[3]){
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
}

struct LibertyBounce {
    LibertyBounce() {
        char module[MAX_PATH]{};
        GetModuleFileNameA(reinterpret_cast<HMODULE>(&__ImageBase),module,MAX_PATH);
        std::string dir(module);dir=dir.substr(0,dir.find_last_of("\\/")+1);
        iniPath=dir+"LibertyBounce.ini";logPath=dir+"LibertyBounce.log";
        Log(std::string("LibertyBounce 0.1.0 startup; ")+plugin::GetGameVersionName());
        // Every address used (SDK events, CReplay::Mode, CCutsceneMgr::ms_running) is v1.0.
        if(plugin::GetGameVersion()!=GAME_10EN) {Log("unsupported executable: LibertyBounce needs gta3.exe v1.0; disabled");return;}
        LoadConfig();
        savedFrames.reserve(256);
        plugin::Events::vehicleRenderEvent.before+=BeforeVehicleRender;
        plugin::Events::pedRenderEvent.before+=BeforePedRender;
        plugin::Events::drawingEvent.before+=Tick;
        plugin::Events::gameProcessEvent.before+=RestoreFrames;
        // Never leave a borrowed frame pointer alive across entity destruction.
        plugin::Events::vehicleDtorEvent.before+=[](CVehicle*){RestoreFrames();};
        plugin::Events::pedDtorEvent.before+=[](CPed*){RestoreFrames();};
        Log("render hooks installed for cars, boats, trains, helicopters, planes and people");
    }
} instance;
}
