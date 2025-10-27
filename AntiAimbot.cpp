#include <stdio.h>
#include <deque>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cctype>

#include "AntiAimbot.h"
#include "metamod_oslink.h"
#include "schemasystem/schemasystem.h"

AntiAimbot g_AntiAimbot;
PLUGIN_EXPOSE(AntiAimbot, g_AntiAimbot);

IVEngineServer2* engine = nullptr;
CGameEntitySystem* g_pGameEntitySystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;
CGlobalVars* gpGlobals = nullptr;

IUtilsApi*   g_pUtils = nullptr;
IPlayersApi* g_pPlayers = nullptr;
IAdminApi*   g_pAdmin = nullptr;

static bool g_debug = true;

static float g_sampleInterval = 0.03f;

static float g_analysisWindow = 0.60f;

static float g_fovLockDeg = 4.0f;
static float g_lockVarDeg = 0.50f;
static float g_lockHoldNeed = 0.25f;

static float g_snapDeltaDeg = 12.0f;
static float g_preSnapMinFov = 10.0f;

static float g_silentFovMin = 20.0f;
static float g_maxShotAge = 0.35f;
static float g_headBonus = 2.0f;

static float g_lockWeight = 10.0f;
static float g_snapWeight = 10.0f;
static float g_silentWeight = 18.0f;

static float g_threshold = 20.0f;

static float g_closeRangeM = 3.0f;
static float g_closeRangeMult = 0.50f;

static float g_decayPerSecond = 1.0f;

static float g_approachWindowS = 0.40f;
static float g_approachNeedFrac = 0.60f;
static float g_approachMinTime = 0.12f;
static float g_approachBonus = 8.0f;

static float g_continuityWindowS = 0.50f;
static float g_continuityWeight = 6.0f;
static float g_continuityMaxSpikeDeg = 9.0f;
static float g_continuityMinAvgDelta = 0.20f;

static int g_bufHardCapSamples = 9000;
static int g_bufDecimateFactor = 2;

static float g_crouchEyeZDeltaMin = 14.0f;
static float g_verticalDomRatio = 0.75f;
static float g_snapNearBoostPerM = 8.0f;
static float g_lockCloseMult = 0.50f;
static float g_snapVerticalBoostPerDeg= 0.5f;
static float g_headshotElevNoBonusDeg = 15.0f;

static float g_lockAcqLookbackS = 0.25f;
static float g_lockAcqMinDeltaDeg = 2.0f;
static float g_lockEnterFovMarginDeg = 0.5f;
static float g_lockAngleHoldReduce = 0.15f;
static float g_angleHoldPreFovDeg = 1.2f;
static float g_angleHoldMaxDeltaDeg = 1.0f;

static float g_exposureWindowS = 0.25f;
static float g_exposureConeDeg = 15.0f;
static float g_exposureNeedS = 0.10f;
static float g_exposureSoftScale = 0.50f;
static float g_farRangeM = 12.0f;

static bool g_banEnabled  = true;
static std::string g_banAction = "admin";
static std::string g_banCommand = "";

static bool g_excludeWarmup = false;
static int g_banMinutes = 0;
static std::string g_reason = "Использование aimbot";

static inline void Dbg(const char* fmt, ...)
{
    if (!g_debug) return;
    char buf[1024];
    va_list va; va_start(va, fmt);
    V_vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);
    ConColorMsg(Color(255,150,50,255), "[AntiAimbot] %s\n", buf);
}

static inline float Now() 
{ 
    return gpGlobals ? gpGlobals->curtime : 0.0f; 
}

static inline std::string ToLower(std::string s) 
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

static inline void ReplaceAll(std::string& s, const std::string& what, const std::string& with) 
{
    if (what.empty()) return;
    size_t pos = 0;
    while ((pos = s.find(what, pos)) != std::string::npos) {
        s.replace(pos, what.size(), with);
        pos += with.size();
    }
}

static inline std::string EscapeForCmd(const std::string& in) 
{
    std::string out; out.reserve(in.size());
    for (char c: in) {
        if (c=='"')  out += "\\\"";
        else if (c=='\n' || c=='\r') out += ' ';
        else out += c;
    }
    return out;
}

static inline std::string Steam64Str(int slot) 
{
    if (auto* c = CCSPlayerController::FromSlot(slot)) {
        uint64 sid = (uint64)c->m_steamID();
        if (sid > 0) return std::to_string(sid);
    }
    return "0";
}

static inline float AngleBetween(const Vector& a, const Vector& b)
{
    float dot = a.x*b.x + a.y*b.y + a.z*b.z;
    float na  = std::sqrt(std::max(1e-12f, a.x*a.x + a.y*a.y + a.z*a.z));
    float nb  = std::sqrt(std::max(1e-12f, b.x*b.x + b.y*b.y + b.z*b.z));
    dot /= (na*nb);
    dot = std::clamp(dot, -1.0f, 1.0f);
    return std::acos(dot) * (180.0f / float(M_PI));
}

static inline bool DirFromTrace(const trace_info_t& tr, Vector& out)
{
    Vector d = tr.m_vEndPos - tr.m_vStartPos;
    float l2 = d.x*d.x + d.y*d.y + d.z*d.z;
    if (l2 < 1e-9f || std::isnan(l2)) return false;
    float inv = 1.0f / std::sqrt(l2);
    out.x = d.x*inv; out.y = d.y*inv; out.z = d.z*inv;
    return true;
}

static inline Vector EyePos(CCSPlayerPawn* pawn)
{
    Vector o = pawn->GetAbsOrigin(); o.z += 64.0f; return o;
}
static inline float UnitsToMeters(float u) 
{ 
    return u * 0.0254f; 
}

static inline float AngleNormalize180(float a)
{
    while (a > 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

static inline float AngleDiffDeg(float a, float b)
{
    return AngleNormalize180(a - b);
}

static inline void DirToAngles(const Vector& d, float& pitch, float& yaw)
{
    yaw   = std::atan2(d.y, d.x) * (180.0f/float(M_PI));
    float hyp = std::sqrt(std::max(1e-12f, d.x*d.x + d.y*d.y));
    pitch = std::atan2(-d.z, hyp) * (180.0f/float(M_PI));
}

static inline void AnglesToDir(float pitch, float yaw, Vector& out)
{
    const float pr = pitch * (float(M_PI)/180.0f);
    const float yr = yaw   * (float(M_PI)/180.0f);
    const float cp = std::cos(pr), sp = std::sin(pr);
    const float cy = std::cos(yr), sy = std::sin(yr);
    out.x = cp*cy;
    out.y = cp*sy;
    out.z = -sp;
}

struct Sample
{
    float t;
    float fov;
    float delta;
    float yaw;
    float pitch;
    float dYaw;
    float dPitch;
    float eyeZ;
};

struct PState
{
    bool haveLastDir=false;
    Vector lastDir{0,0,0};
    float lastYaw=0.0f, lastPitch=0.0f;

    std::deque<Sample> buf;
    float lastShotT = -1.0f;
    float lastKillT = -1.0f;

    float suspicion = 0.0f;
};
static PState g_ps[64];

static CTimer* g_sampler = nullptr;

static float FovToClosestEnemy(int slot, const Vector& aimDir)
{
    CCSPlayerController* me = CCSPlayerController::FromSlot(slot);
    if (!me) return 180.0f;
    CCSPlayerPawn* myPawn = me->GetPlayerPawn();
    if (!myPawn) return 180.0f;

    int myTeam = me->m_iTeamNum();
    Vector myEye = EyePos(myPawn);
    float best = 180.0f;

    for (int j=0;j<64;++j)
    {
        if (j==slot) continue;
        if (!g_pPlayers->IsInGame(j) || g_pPlayers->IsFakeClient(j)) continue;

        CCSPlayerController* op = CCSPlayerController::FromSlot(j);
        if (!op) continue;
        if (op->m_iTeamNum() == myTeam) continue;

        CCSPlayerPawn* pp = op->GetPlayerPawn();
        if (!pp || !op->m_bPawnIsAlive()) continue;

        Vector dir = EyePos(pp) - myEye;
        float len2 = dir.x*dir.x + dir.y*dir.y + dir.z*dir.z;
        if (len2 < 1e-6f) continue;
        float inv = 1.0f/std::sqrt(len2);
        dir.x*=inv; dir.y*=inv; dir.z*=inv;

        float fov = AngleBetween(aimDir, dir);
        if (fov < best) best = fov;
    }
    return best;
}

static void DecimateIfNeeded(std::deque<Sample>& buf)
{
    if ((int)buf.size() <= g_bufHardCapSamples) return;
    std::deque<Sample> out; out.clear();
    int skip = std::max(2, g_bufDecimateFactor);
    int half = (int)buf.size()/2;
    for (int i=0;i<half;++i) if ((i%skip)!=0) out.push_back(buf[i]);
    for (size_t i=half;i<buf.size();++i) out.push_back(buf[i]);
    buf.swap(out);
}

static void PushSample(int slot)
{
    trace_info_t tr = g_pPlayers->RayTrace(slot);
    Vector dir;
    if (!DirFromTrace(tr, dir)) return;

    float yaw=0.0f, pitch=0.0f;
    DirToAngles(dir, pitch, yaw);

    float delta = 0.0f;
    if (g_ps[slot].haveLastDir)
        delta = AngleBetween(g_ps[slot].lastDir, dir);

    float dYaw = 0.0f, dPitch = 0.0f;
    if (g_ps[slot].haveLastDir){
        dYaw = std::fabs(AngleDiffDeg(yaw, g_ps[slot].lastYaw));
        dPitch = std::fabs(AngleDiffDeg(pitch, g_ps[slot].lastPitch));
    }

    float fov = FovToClosestEnemy(slot, dir);

    float eyeZ = 0.0f;
    if (auto* c = CCSPlayerController::FromSlot(slot)){
        if (auto* p = c->GetPlayerPawn()){
            eyeZ = EyePos(p).z;
        }
    }

    g_ps[slot].lastDir = dir;
    g_ps[slot].haveLastDir = true;
    g_ps[slot].lastYaw = yaw;
    g_ps[slot].lastPitch = pitch;

    g_ps[slot].buf.push_back({ Now(), fov, delta, yaw, pitch, dYaw, dPitch, eyeZ });
    DecimateIfNeeded(g_ps[slot].buf);
}

static bool GetLastSampleBefore(const std::deque<Sample>& buf, float tRef, Sample& out)
{
    for (int i=int(buf.size())-1; i>=0; --i)
        if (buf[i].t <= tRef) { out = buf[i]; return true; }
    if (!buf.empty()) { out = buf.back(); return true; }
    return false;
}

static float SampleTick()
{
    for (int i=0;i<64;++i)
    {
        if (!g_pPlayers->IsInGame(i) || g_pPlayers->IsFakeClient(i)) continue;
        CCSPlayerController* c = CCSPlayerController::FromSlot(i);
        if (!c || !c->m_bPawnIsAlive()) continue;

        g_ps[i].suspicion = std::max(0.0f, g_ps[i].suspicion - g_decayPerSecond * g_sampleInterval);

        PushSample(i);
    }
    return g_sampleInterval;
}

static void WindowStats(const std::deque<Sample>& buf, float tFrom, float tTo, float& maxDelta, float& preFov, float& lockFrac, float& avgDelta)
{
    maxDelta=0.0f; preFov=180.0f; lockFrac=0.0f; avgDelta=0.0f;
    float timeTotal=0.0f, timeLocked=0.0f;
    bool have=false; float lastT=0.0f; float sumDelta=0.0f; int cnt=0;

    for (const auto& s: buf)
    {
        if (s.t < tFrom || s.t > tTo) continue;
        preFov = std::min(preFov, s.fov);
        maxDelta = std::max(maxDelta, s.delta);
        sumDelta += s.delta; ++cnt;
        if (!have) { have=true; lastT=s.t; continue; }
        float dt = s.t - lastT; if (dt > 0.0f)
        {
            timeTotal += dt;
            if (s.fov <= g_fovLockDeg && s.delta <= g_lockVarDeg) timeLocked += dt;
        }
        lastT = s.t;
    }
    lockFrac = (timeTotal>0.0f) ? (timeLocked/timeTotal) : 0.0f;
    avgDelta = (cnt>0) ? (sumDelta/float(cnt)) : 0.0f;
}

static float AnalyzeSnap(const std::deque<Sample>& buf, float tKill, float windowS, float& preFovOut, float& maxDeltaOut)
{
    float tFrom=tKill-windowS; float maxD=0.0f, preF=180.0f, lockF=0.0f, avgD=0.0f;
    WindowStats(buf, tFrom, tKill, maxD, preF, lockF, avgD);
    preFovOut=preF; maxDeltaOut=maxD;
    if (maxD >= g_snapDeltaDeg && preF >= g_preSnapMinFov) return g_snapWeight;
    return 0.0f;
}

static float AnalyzeLockHold(const std::deque<Sample>& buf, float tKill, float windowS, float& bestHoldOut, bool& bestWasAcqOut, float& debugPrevFovOut, float& debugPreMaxDeltaOut)
{
    float tFrom = tKill - windowS;
    float bestHold = 0.0f; bool bestAcq = false;
    float dbgPrevFov = 180.0f, dbgPreMaxDelta = 0.0f;

    bool in=false; float start=-1.0f; float last=-1.0f;

    auto finishSpan = [&](float spanStart, float spanEnd){
        float hold = std::max(0.0f, spanEnd - spanStart);
        if (hold <= bestHold) return;

        Sample prev{};
        bool havePrev = GetLastSampleBefore(buf, spanStart - 1e-4f, prev);
        float prevFov = havePrev ? prev.fov : 180.0f;

        float lbFrom = spanStart - g_lockAcqLookbackS;
        float preMaxDelta = 0.0f, preSumDelta = 0.0f;
        for (const auto& s: buf)
        {
            if (s.t >= lbFrom && s.t < spanStart)
            {
                preMaxDelta = std::max(preMaxDelta, s.delta);
                preSumDelta += s.delta;
            }
        }

        bool enteredFromOutside = (prevFov > (g_fovLockDeg + g_lockEnterFovMarginDeg));
        bool enoughMotion = (preMaxDelta >= g_lockAcqMinDeltaDeg) || (preSumDelta >= g_lockAcqMinDeltaDeg);
        bool acquisition = enteredFromOutside && enoughMotion;

        bool pureAngleHold = (prevFov <= g_angleHoldPreFovDeg) && (preMaxDelta <= g_angleHoldMaxDeltaDeg);

        bestHold = hold;
        bestAcq = acquisition && !pureAngleHold;

        dbgPrevFov = prevFov;
        dbgPreMaxDelta = preMaxDelta;
    };

    for (const auto& s: buf)
    {
        if (s.t < tFrom || s.t > tKill) continue;
        bool locked = (s.fov <= g_fovLockDeg) && (s.delta <= g_lockVarDeg);
        if (locked)
        {
            if (!in) { in=true; start=s.t; }
            last = s.t;
        }
        else
        {
            if (in) { finishSpan(start, last); in=false; }
        }
    }
    if (in) { finishSpan(start, (buf.empty()?tKill:std::min(tKill, buf.back().t))); }

    bestHoldOut = bestHold;
    bestWasAcqOut = bestAcq;
    debugPrevFovOut = dbgPrevFov;
    debugPreMaxDeltaOut = dbgPreMaxDelta;

    if (bestHold < g_lockHoldNeed) return 0.0f;

    float mult  = std::min(2.0f, bestHold / g_lockHoldNeed);
    float score = g_lockWeight * mult;

    if (!bestAcq)
    {
        if (dbgPrevFov <= g_angleHoldPreFovDeg && dbgPreMaxDelta <= g_angleHoldMaxDeltaDeg)
            return 0.0f;
        score *= g_lockAngleHoldReduce;
    }
    return score;
}

static float AnalyzeContinuity(const std::deque<Sample>& buf, float tKill, float windowS)
{
    float tFrom=tKill-windowS; float maxD=0.0f, preF=180.0f, lockF=0.0f, avgD=0.0f;
    WindowStats(buf, tFrom, tKill, maxD, preF, lockF, avgD);
    if (maxD >= g_continuityMaxSpikeDeg && avgD <= g_continuityMinAvgDelta) return g_continuityWeight;
    return 0.0f;
}

static float HumanApproachRelief(const std::deque<Sample>& buf, float tKill, float windowS, float needFrac, float minTime, float bonus)
{
    float tFrom=tKill-windowS; bool have=false;
    float lastFov=0.0f, lastT=0.0f, timeTotal=0.0f, timeDec=0.0f;

    for (const auto& s: buf)
    {
        if (s.t < tFrom || s.t > tKill) continue;
        if (!have) { have=true; lastFov=s.fov; lastT=s.t; continue; }
        float dt = s.t - lastT;
        if (dt>0.0f) { timeTotal += dt; if (s.fov <= lastFov) timeDec += dt; }
        lastFov=s.fov; lastT=s.t;
    }
    float frac = (timeTotal>0.0f) ? (timeDec/timeTotal) : 0.0f;
    if (timeTotal >= minTime && frac >= needFrac) return bonus;
    return 0.0f;
}

static void ComputeBaseline(const std::deque<Sample>& buf, float tKill, float& baseMaxDelta, float& baseAvgDelta, float& baseLockFrac)
{
    float maxD=0.0f, preF=180.0f, lockF=0.0f, avgD=0.0f;
    float tStart = buf.empty()? tKill-600.0f : buf.front().t;
    WindowStats(buf, tStart, tKill, maxD, preF, lockF, avgD);
    baseMaxDelta=maxD; baseAvgDelta=avgD; baseLockFrac=lockF;
}

static bool CrouchAdjustSeen(const std::deque<Sample>& buf, float tFrom, float tTo, float minDeltaEyeZ)
{
    bool first=true; float minZ=0.0f, maxZ=0.0f;
    for (const auto& s: buf){
        if (s.t < tFrom || s.t > tTo) continue;
        if (first){ minZ=maxZ=s.eyeZ; first=false; }
        else { if (s.eyeZ < minZ) minZ=s.eyeZ; if (s.eyeZ > maxZ) maxZ=s.eyeZ; }
    }
    return (!first) && ((maxZ - minZ) >= minDeltaEyeZ);
}

static bool VerticalDominant(const std::deque<Sample>& buf, float tFrom, float tTo, float needRatio)
{
    double sumYaw=0.0, sumPitch=0.0;
    for (const auto& s: buf){
        if (s.t < tFrom || s.t > tTo) continue;
        sumYaw   += std::fabs(s.dYaw);
        sumPitch += std::fabs(s.dPitch);
    }
    double denom = sumYaw + sumPitch;
    if (denom <= 1e-6) return false;
    return (sumPitch / denom) >= needRatio;
}

static bool FovToVictimAtShot(int attacker, int victim, float tKill, float& outFovDeg, float& outPitchToVictimDeg)
{
    CCSPlayerController* atk = CCSPlayerController::FromSlot(attacker);
    CCSPlayerController* vic = CCSPlayerController::FromSlot(victim);
    if (!atk || !vic) return false;
    CCSPlayerPawn* ap = atk->GetPlayerPawn();
    CCSPlayerPawn* vp = vic->GetPlayerPawn();
    if (!ap || !vp) return false;

    Vector eyeA = EyePos(ap);
    Vector eyeV = EyePos(vp);
    Vector dirAV = eyeV - eyeA;
    float len2 = dirAV.x*dirAV.x + dirAV.y*dirAV.y + dirAV.z*dirAV.z;
    if (len2 < 1e-6f) return false;
    float inv = 1.0f/std::sqrt(len2);
    dirAV.x*=inv; dirAV.y*=inv; dirAV.z*=inv;

    Sample s{};
    if (!GetLastSampleBefore(g_ps[attacker].buf, tKill, s)) return false;

    Vector aim;
    AnglesToDir(s.pitch, s.yaw, aim);

    outFovDeg = AngleBetween(aim, dirAV);

    float horiz = std::sqrt(std::max(1e-12f, (eyeV.x-eyeA.x)*(eyeV.x-eyeA.x) + (eyeV.y-eyeA.y)*(eyeV.y-eyeA.y)));
    outPitchToVictimDeg = std::fabs(std::atan2((eyeV.z-eyeA.z), horiz) * (180.0f/float(M_PI)));
    return true;
}

static void ApplyRelief(int slot, float relief, const char* why)
{
    if (relief <= 0.0f) return;
    float before=g_ps[slot].suspicion;
    g_ps[slot].suspicion = std::max(0.0f, g_ps[slot].suspicion - relief);
    Dbg("relief slot=%d -=%.1f %.1f->%.1f (%s)", slot, relief, before, g_ps[slot].suspicion, why?why:"");
}

static void TryBan(int slot, float add, const char* why)
{
    if (add <= 0.0f) return;
    g_ps[slot].suspicion += add;
    Dbg("susp slot=%d +=%.1f -> %.1f (%s)", slot, add, g_ps[slot].suspicion, why?why:"");

    if (g_excludeWarmup) {
        if (auto* rules = g_pUtils ? g_pUtils->GetCCSGameRules() : nullptr; rules && rules->m_bWarmupPeriod())
            return;
    }
    if (g_ps[slot].suspicion < g_threshold) return;

    if (!g_banEnabled || g_banAction == "none") {
        Dbg("DETECTED slot=%d score=%.1f thr=%.1f (ban disabled/none).", slot, g_ps[slot].suspicion, g_threshold);
        g_ps[slot].suspicion = 0.0f;
        return;
    }

    const std::string sid64    = Steam64Str(slot);
    const std::string minutes  = std::to_string(g_banMinutes);
    const std::string reasonRaw= EscapeForCmd(g_reason);
    const std::string reasonQ  = std::string("\"") + reasonRaw + "\"";

    bool done = false;

    if (g_banAction == "command" && !g_banCommand.empty() && engine) {
        std::string cmd = g_banCommand;

        ReplaceAll(cmd, "{slot}",       std::to_string(slot));
        ReplaceAll(cmd, "{userid}",     std::to_string(slot));
        ReplaceAll(cmd, "{steamid64}",  sid64);
        ReplaceAll(cmd, "{minutes}",    minutes);
        ReplaceAll(cmd, "{reason_q}",   reasonQ);
        ReplaceAll(cmd, "{reason}",     reasonRaw);

        if (cmd.empty() || cmd.back() != '\n') cmd += "\n";
        engine->ServerCommand(cmd.c_str());
        Dbg("BAN slot=%d via custom command: %s", slot, g_banCommand.c_str());
        done = true;
    }
    else if (g_banAction == "admin" && g_pAdmin) {
        g_pAdmin->AddPlayerPunishment(slot, RT_BAN, g_banMinutes, g_reason.c_str());
        Dbg("BAN slot=%d via AdminApi minutes=%d reason='%s'", slot, g_banMinutes, g_reason.c_str());
        done = true;
    }
    else if (g_banAction == "disconnect" && engine) {
        engine->DisconnectClient(CPlayerSlot(slot), NETWORK_DISCONNECT_KICKED);
        Dbg("BAN slot=%d via disconnect reason='%s'", slot, g_reason.c_str());
        done = true;
    }
    else {
        if (g_pAdmin) {
            g_pAdmin->AddPlayerPunishment(slot, RT_BAN, g_banMinutes, g_reason.c_str());
            Dbg("BAN slot=%d via fallback AdminApi minutes=%d reason='%s'", slot, g_banMinutes, g_reason.c_str());
            done = true;
        } else if (engine) {
            engine->DisconnectClient(CPlayerSlot(slot), NETWORK_DISCONNECT_KICKED);
            Dbg("BAN slot=%d via fallback disconnect reason='%s'", slot, g_reason.c_str());
            done = true;
        } else {
            Dbg("DETECTED slot=%d but no ban path available.", slot);
        }
    }

    if (done) g_ps[slot].suspicion = 0.0f;
}

static inline std::string NormalizeWeaponName(const char* w)
{
    if (!w) return std::string();
    std::string s(w);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    if (s.rfind("weapon_", 0) == 0) s.erase(0, 7);
    return s;
}

static inline bool IsGrenadeName(const std::string& s)
{
    return s=="hegrenade" || s=="flashbang" || s=="smokegrenade" ||
           s=="molotov"  || s=="decoy" || s=="tagrenade" ||
           s=="inferno";
}

static inline bool IsGrenadeEvent(IGameEvent* ev)
{
    const char* w = ev->GetString("weapon", "");
    if (!w || !*w) w = ev->GetString("weapon_fqn", "");
    if (!w || !*w) w = ev->GetString("weaponid", "");
    std::string n = NormalizeWeaponName(w);
    return IsGrenadeName(n);
}

static void OnWeaponFire(const char*, IGameEvent* ev, bool)
{
    int slot = ev->GetInt("userid");
    if (slot < 0 || slot >= 64) return;

    if (IsGrenadeEvent(ev)) {
        if (g_debug) {
            const char* w = ev->GetString("weapon", "");
            Dbg("weapon_fire ignored (grenade): slot=%d w='%s'", slot, w && *w ? w : "?");
        }
        return;
    }

    g_ps[slot].lastShotT = Now();
    Dbg("shot slot=%d t=%.3f", slot, g_ps[slot].lastShotT);
}

static float VictimExposure(int attacker, int victim, float tKill, float windowS, float coneDeg)
{
    const auto& buf = g_ps[attacker].buf;
    if (buf.empty()) return 0.0f;

    CCSPlayerController* atk = CCSPlayerController::FromSlot(attacker);
    CCSPlayerController* vic = CCSPlayerController::FromSlot(victim);
    if (!atk || !vic) return 0.0f;
    CCSPlayerPawn* ap = atk->GetPlayerPawn();
    CCSPlayerPawn* vp = vic->GetPlayerPawn();
    if (!ap || !vp) return 0.0f;

    Vector eyeA = EyePos(ap), eyeV = EyePos(vp);
    Vector toV = eyeV - eyeA; float l2 = toV.x*toV.x + toV.y*toV.y + toV.z*toV.z;
    if (l2 < 1e-6f) return 0.0f; float inv = 1.0f/std::sqrt(l2);
    toV.x*=inv; toV.y*=inv; toV.z*=inv;

    float tFrom = tKill - windowS;
    bool have=false, lastIn=false; float lastT=0.0f, vis=0.0f;

    for (const auto& s: buf) {
        if (s.t < tFrom || s.t > tKill) continue;
        Vector aim; AnglesToDir(s.pitch, s.yaw, aim);
        float fov = AngleBetween(aim, toV); bool in = (fov <= coneDeg);
        if (!have) { have=true; lastT=s.t; lastIn=in; continue; }
        float dt = s.t - lastT; if (dt>0.0f && lastIn) vis += dt;
        lastT = s.t; lastIn = in;
    }
    if (have && lastIn) vis += std::max(0.0f, tKill - lastT);
    return vis;
}

static void OnPlayerDeath(const char*, IGameEvent* ev, bool)
{
    if (IsGrenadeEvent(ev)) {
        if (g_debug) {
            const char* w = ev->GetString("weapon", "");
            Dbg("kill ignored (grenade): w='%s'", w && *w ? w : "?");
        }
        return;
    }

    int victim   = ev->GetInt("userid");
    int attacker = ev->GetInt("attacker");
    bool head    = ev->GetInt("headshot") != 0;
    if (attacker < 0 || attacker >= 64 || victim < 0 || victim >= 64 || attacker == victim) return;

    float distM = -1.0f;
    if (auto* atk = CCSPlayerController::FromSlot(attacker)) {
        if (auto* vic = CCSPlayerController::FromSlot(victim)) {
            if (auto* ap = atk->GetPlayerPawn()) {
                if (auto* vp = vic->GetPlayerPawn()) {
                    Vector d = EyePos(vp) - EyePos(ap);
                    float u = std::sqrt(std::max(0.0f, d.x*d.x + d.y*d.y + d.z*d.z));
                    distM = UnitsToMeters(u);
                }
            }
        }
    }

    float tKill = Now();
    g_ps[attacker].lastKillT = tKill;

    float tFrom = tKill - g_analysisWindow;

    bool crouchAdj = CrouchAdjustSeen(g_ps[attacker].buf, tFrom, tKill, g_crouchEyeZDeltaMin);
    bool vertDom   = VerticalDominant  (g_ps[attacker].buf, tFrom, tKill, g_verticalDomRatio);

    float fovVictimShot = 0.0f, pitchToVictimDeg = 0.0f;
    bool  haveVictimGeom = FovToVictimAtShot(attacker, victim, tKill, fovVictimShot, pitchToVictimDeg);

    float addSilent = 0.0f;
    if (g_ps[attacker].lastShotT > 0.0f && (tKill - g_ps[attacker].lastShotT) <= g_maxShotAge && haveVictimGeom)
    {
        bool closeRange = (distM > 0.0f && distM <= g_closeRangeM);
        if (!closeRange && fovVictimShot >= g_silentFovMin)
        {
            addSilent = g_silentWeight;
            if (head && pitchToVictimDeg < g_headshotElevNoBonusDeg)
                addSilent *= g_headBonus;
        }
    }

    float preFov = 0.0f, maxDelta = 0.0f;
    float addSnap = AnalyzeSnap(g_ps[attacker].buf, tKill, g_analysisWindow, preFov, maxDelta);

    float nearBoost = 0.0f;
    if (distM > 0.0f && distM < g_closeRangeM)
        nearBoost = (g_closeRangeM - distM) * g_snapNearBoostPerM;

    float verticalBoost = 0.0f;
    if (haveVictimGeom && pitchToVictimDeg > 0.0f)
        verticalBoost = g_snapVerticalBoostPerDeg * pitchToVictimDeg;

    float scaledSnapThreshold = g_snapDeltaDeg + nearBoost + verticalBoost;
    if (addSnap > 0.0f && maxDelta < scaledSnapThreshold)
        addSnap = 0.0f;

    if (addSnap > 0.0f && crouchAdj && vertDom)
        addSnap *= 0.2f;

    float bestHold = 0.0f; bool bestWasAcq = false; float dbgPrevFov = 180.0f, dbgPreMaxDelta = 0.0f;
    float addLock = AnalyzeLockHold(g_ps[attacker].buf, tKill, g_analysisWindow,
                                    bestHold, bestWasAcq, dbgPrevFov, dbgPreMaxDelta);

    if (addLock > 0.0f)
    {
        if (distM > 0.0f && distM <= g_closeRangeM) addLock *= g_lockCloseMult;
        if (crouchAdj)                               addLock *= 0.5f;
    }

    float addCont = AnalyzeContinuity(g_ps[attacker].buf, tKill, std::min(g_continuityWindowS, g_analysisWindow));

    float baseMaxD = 0.0f, baseAvgD = 0.0f, baseLockF = 0.0f;
    ComputeBaseline(g_ps[attacker].buf, tKill, baseMaxD, baseAvgD, baseLockF);
    if (addSnap > 0.0f && maxDelta <= baseMaxD + 3.0f) addSnap *= 0.5f;

    float exposureS     = VictimExposure(attacker, victim, tKill, std::min(g_exposureWindowS, g_analysisWindow), g_exposureConeDeg);
    bool  exposureEnough= (exposureS >= g_exposureNeedS);
    bool  isFar         = (distM > 0.0f && distM >= g_farRangeM);

    auto killIfLowExposure = [&](float& score, bool hardZero){
        if (score <= 0.0f) return;
        if (exposureEnough || isFar) return;
        if (hardZero) score = 0.0f; else score *= g_exposureSoftScale;
    };

    killIfLowExposure(addSilent, /*hardZero*/ true);
    killIfLowExposure(addSnap,   /*hardZero*/ true);
    if (!exposureEnough && !isFar && addLock > 0.0f) addLock *= g_exposureSoftScale;

    float relief = HumanApproachRelief(g_ps[attacker].buf, tKill,
                                       std::min(g_approachWindowS, g_analysisWindow),
                                       g_approachNeedFrac, g_approachMinTime, g_approachBonus);

    float addTotal = addSilent + addSnap + addLock + addCont;
    if (distM > 0.0f && distM <= g_closeRangeM)
        addTotal *= g_closeRangeMult;

    if (relief > 0.0f) ApplyRelief(attacker, relief, "approach");

    Dbg("kill atk=%d vic=%d dist=%.2fm add=%.1f "
        "snap{max=%.1f thr=%.1f preFov=%.1f} lock{hold=%.2fs acq=%d prevFov=%.1f preMaxΔ=%.1f need=%.2fs} "
        "silent{fovVictim=%.1f pitchToVictim=%.1f} cont{w=%.1f} "
        "baseline{max=%.1f avg=%.2f lock=%.2f} "
        "ctx{crouch=%d vertDom=%d expo=%.3fs enough=%d}",
        attacker, victim, (distM>0.0f?distM:-1.0f), addTotal,
        maxDelta, scaledSnapThreshold, preFov,
        bestHold, (int)bestWasAcq, dbgPrevFov, dbgPreMaxDelta, g_lockHoldNeed,
        (haveVictimGeom?fovVictimShot:-1.0f), (haveVictimGeom?pitchToVictimDeg:-1.0f), addCont,
        baseMaxD, baseAvgD, baseLockF,
        (int)crouchAdj, (int)vertDom, exposureS, (int)exposureEnough
    );

    TryBan(attacker, addTotal, "score");
}

static void OnRoundStart(const char*, IGameEvent*, bool)
{
    for (int i=0;i<64;++i)
    {
        g_ps[i].buf.clear();
        g_ps[i].haveLastDir = false;
        g_ps[i].lastYaw = g_ps[i].lastPitch = 0.0f;
        g_ps[i].lastShotT = -1.0f;
        g_ps[i].lastKillT = -1.0f;
        g_ps[i].suspicion = 0.0f;
    }
    Dbg("round_start: buffers reset");

    if (g_pUtils)
    {
        if (g_sampler) { g_pUtils->RemoveTimer(g_sampler); g_sampler = nullptr; }
        g_sampler = g_pUtils->CreateTimer(g_sampleInterval, [](){ return SampleTick(); });
        Dbg("sampler started @ %.3fs", g_sampleInterval);
    }
}

CGameEntitySystem* GameEntitySystem()
{
    return g_pUtils->GetCGameEntitySystem();
}

void StartupServer()
{
    g_pGameEntitySystem = g_pUtils->GetCGameEntitySystem();
    g_pEntitySystem = g_pUtils->GetCEntitySystem();
    gpGlobals = g_pUtils->GetCGlobalVars();
}

static void LoadConfig()
{
    KeyValues::AutoDelete kv("AntiAimbot");
    if (!kv->LoadFromFile(g_pFullFileSystem, "addons/configs/AntiAimbot/settings.ini"))
    {
        Dbg("No settings.ini, defaults in use");
        return;
    }

    g_debug = kv->GetBool ("debug", g_debug);

    g_sampleInterval = std::max(0.005f, kv->GetFloat("sample_interval", g_sampleInterval));

    g_analysisWindow = kv->GetFloat("analysis_window_s", g_analysisWindow);

    g_fovLockDeg = kv->GetFloat("fov_lock_deg", g_fovLockDeg);
    g_lockVarDeg = kv->GetFloat("lock_var_deg", g_lockVarDeg);
    g_lockHoldNeed = kv->GetFloat("lock_hold_s", g_lockHoldNeed);

    g_snapDeltaDeg = kv->GetFloat("snap_delta_deg", g_snapDeltaDeg);
    g_preSnapMinFov = kv->GetFloat("pre_snap_min_fov", g_preSnapMinFov);

    g_silentFovMin = kv->GetFloat("silent_fov_min", g_silentFovMin);
    g_silentWeight = kv->GetFloat("silent_weight", g_silentWeight);
    g_maxShotAge = kv->GetFloat("max_shot_age", g_maxShotAge);
    g_headBonus = kv->GetFloat("head_bonus", g_headBonus);

    g_lockWeight = kv->GetFloat("lock_weight", g_lockWeight);
    g_snapWeight = kv->GetFloat("snap_weight", g_snapWeight);
    g_threshold = kv->GetFloat("threshold", g_threshold);

    g_excludeWarmup = kv->GetBool ("exclude_warmup", g_excludeWarmup);
    g_banMinutes = kv->GetInt  ("ban_time_minutes", g_banMinutes);
    g_reason = kv->GetString("reason", g_reason.c_str());

    g_closeRangeM = kv->GetFloat("close_range_m", g_closeRangeM);
    g_closeRangeMult = kv->GetFloat("close_range_mult", g_closeRangeMult);
    g_decayPerSecond = kv->GetFloat("decay_per_second", g_decayPerSecond);

    g_approachWindowS = kv->GetFloat("approach_window_s", g_approachWindowS);
    g_approachNeedFrac = kv->GetFloat("approach_need_frac", g_approachNeedFrac);
    g_approachMinTime = kv->GetFloat("approach_min_time", g_approachMinTime);
    g_approachBonus = kv->GetFloat("approach_bonus", g_approachBonus);

    g_continuityWindowS = kv->GetFloat("continuity_window_s", g_continuityWindowS);
    g_continuityWeight = kv->GetFloat("continuity_weight", g_continuityWeight);
    g_continuityMaxSpikeDeg = kv->GetFloat("continuity_max_spike_deg", g_continuityMaxSpikeDeg);
    g_continuityMinAvgDelta = kv->GetFloat("continuity_min_avg_delta", g_continuityMinAvgDelta);

    g_bufHardCapSamples = kv->GetInt("buf_hard_cap", g_bufHardCapSamples);
    g_bufDecimateFactor = kv->GetInt("buf_decimate_factor", g_bufDecimateFactor);

    g_crouchEyeZDeltaMin = kv->GetFloat("crouch_eyez_delta_min", g_crouchEyeZDeltaMin);
    g_verticalDomRatio = kv->GetFloat("vertical_dom_ratio",    g_verticalDomRatio);
    g_snapNearBoostPerM = kv->GetFloat("snap_near_boost_per_m", g_snapNearBoostPerM);
    g_lockCloseMult = kv->GetFloat("lock_close_mult",       g_lockCloseMult);
    g_snapVerticalBoostPerDeg = kv->GetFloat("snap_vertical_boost_per_deg", g_snapVerticalBoostPerDeg);
    g_headshotElevNoBonusDeg  = kv->GetFloat("headshot_elev_no_bonus_deg",  g_headshotElevNoBonusDeg);

    g_lockAcqLookbackS      = kv->GetFloat("lock_acq_lookback_s",      g_lockAcqLookbackS);
    g_lockAcqMinDeltaDeg    = kv->GetFloat("lock_acq_min_delta_deg",   g_lockAcqMinDeltaDeg);
    g_lockEnterFovMarginDeg = kv->GetFloat("lock_enter_fov_margin_deg",g_lockEnterFovMarginDeg);
    g_lockAngleHoldReduce   = kv->GetFloat("lock_angle_hold_reduce",   g_lockAngleHoldReduce);
    g_angleHoldPreFovDeg    = kv->GetFloat("angle_hold_prefov_deg",    g_angleHoldPreFovDeg);
    g_angleHoldMaxDeltaDeg  = kv->GetFloat("angle_hold_maxdelta_deg",  g_angleHoldMaxDeltaDeg);

    g_exposureWindowS   = kv->GetFloat("exposure_window_s",   g_exposureWindowS);
    g_exposureConeDeg   = kv->GetFloat("exposure_cone_deg",   g_exposureConeDeg);
    g_exposureNeedS     = kv->GetFloat("exposure_need_s",     g_exposureNeedS);
    g_exposureSoftScale = kv->GetFloat("exposure_soft_scale", g_exposureSoftScale);
    g_farRangeM         = kv->GetFloat("far_range_m",         g_farRangeM);

    g_banEnabled = kv->GetBool("ban_enabled", g_banEnabled);
    const char* act = kv->GetString("ban_action", g_banAction.c_str());
    g_banAction = ToLower(act ? act : g_banAction.c_str());
    g_banCommand = kv->GetString("ban_command", g_banCommand.c_str());

    Dbg("cfg: dt=%.3f win=%.2f | cap=%d decim=%d | "
        "close<=%.2fm x%.2f | crouchΔZ>=%.1f vertDom>=%.2f | "
        "snapNear+%.1f/м snapVert+%.1f/° headshotNoBonus>=%.1f° | "
        "lockAcq lb=%.2fs minΔ=%.1f enter+%.1f° reduce×%.2f AHcut preFov<=%.1f° Δ<=%.1f°",
        g_sampleInterval, g_analysisWindow, g_bufHardCapSamples, g_bufDecimateFactor,
        g_closeRangeM, g_closeRangeMult, g_crouchEyeZDeltaMin, g_verticalDomRatio,
        g_snapNearBoostPerM, g_snapVerticalBoostPerDeg, g_headshotElevNoBonusDeg,
        g_lockAcqLookbackS, g_lockAcqMinDeltaDeg, g_lockEnterFovMarginDeg, g_lockAngleHoldReduce,
        g_angleHoldPreFovDeg, g_angleHoldMaxDeltaDeg);
}

bool AntiAimbot::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
    PLUGIN_SAVEVARS();

    GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
    GET_V_IFACE_ANY (GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);

    g_SMAPI->AddListener(this, this);
    return true;
}

bool AntiAimbot::Unload(char* error, size_t maxlen)
{
    ConVar_Unregister();
    if (g_pUtils) g_pUtils->ClearAllHooks(g_PLID);
    if (g_sampler && g_pUtils) { g_pUtils->RemoveTimer(g_sampler); g_sampler=nullptr; }
    return true;
}

void AntiAimbot::AllPluginsLoaded()
{
    char error[64]; int ret;

    g_pUtils = (IUtilsApi*)g_SMAPI->MetaFactory(Utils_INTERFACE, &ret, NULL);
    if (ret == META_IFACE_FAILED || !g_pUtils)
    {
        g_SMAPI->Format(error, sizeof(error), "Missing Utils system plugin");
        ConColorMsg(Color(255,0,0,255), "[%s] %s\n", GetLogTag(), error);
        std::string s = "meta unload " + std::to_string(g_PLID);
        if (engine) engine->ServerCommand(s.c_str());
        return;
    }

    g_pPlayers = (IPlayersApi*)g_SMAPI->MetaFactory(PLAYERS_INTERFACE, &ret, NULL);
    if (ret == META_IFACE_FAILED || !g_pPlayers)
    {
        g_SMAPI->Format(error, sizeof(error), "Missing Players system plugin");
        ConColorMsg(Color(255,0,0,255), "[%s] %s\n", GetLogTag(), error);
        std::string s = "meta unload " + std::to_string(g_PLID);
        if (engine) engine->ServerCommand(s.c_str());
        return;
    }

    g_pAdmin = (IAdminApi*)g_SMAPI->MetaFactory(Admin_INTERFACE, &ret, NULL);
    if (ret == META_IFACE_FAILED || !g_pAdmin)
    {
        g_SMAPI->Format(error, sizeof(error), "Missing IAdminApi system plugin");
        ConColorMsg(Color(255,0,0,255), "[%s] %s\n", GetLogTag(), error);
        std::string s = "meta unload " + std::to_string(g_PLID);
        if (engine) engine->ServerCommand(s.c_str());
        return;
    }

    g_pUtils->StartupServer(g_PLID, StartupServer);

    g_pUtils->HookEvent(g_PLID, "weapon_fire",  OnWeaponFire);
    g_pUtils->HookEvent(g_PLID, "player_death", OnPlayerDeath);
    g_pUtils->HookEvent(g_PLID, "round_start",  OnRoundStart);

    LoadConfig();
}

const char* AntiAimbot::GetLicense()
{
    return "GPL";
}

const char* AntiAimbot::GetVersion()
{
    return "1.0.1";
}
 
const char* AntiAimbot::GetDate()
{
    return __DATE__;
}

const char *AntiAimbot::GetLogTag()
{
    return "[AntiAimbot]";
}

const char* AntiAimbot::GetAuthor()
{
    return "ABKAM";
}

const char* AntiAimbot::GetDescription()
{
    return "Anti-Aimbot";
}

const char* AntiAimbot::GetName()
{
    return "Anti-Aimbot";
}

const char* AntiAimbot::GetURL()
{
    return "https://discord.gg/ChYfTtrtmS";
}
