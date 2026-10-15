//
// Copyright (c) 2008-2022 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../Precompiled.h"

#include "../Core/CoreEvents.h"
#include "../Core/Profiler.h"
#include "../Core/Timer.h"

#include <SDL_timer.h>

#include <chrono>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#elif __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif

#include "../DebugNew.h"

namespace Urho3D
{

bool HiresTimer::supported(false);
long long HiresTimer::frequency(1000);

/// Length in milliseconds of the real-time window over which Time measures the presented frame rate.
static const unsigned FPS_WINDOW_MS = 500;

Time::Time(Context* context) :
    Object(context),
    frameNumber_(0),
    timeStep_(0.0f),
    timerPeriod_(0)
{
#ifdef _WIN32
    LARGE_INTEGER frequency;
    if (QueryPerformanceFrequency(&frequency))
    {
        HiresTimer::frequency = frequency.QuadPart;
        HiresTimer::supported = true;
    }
#else
    HiresTimer::frequency = 1000000;
    HiresTimer::supported = true;
#endif
}

Time::~Time()
{
    SetTimerPeriod(0);
}

#ifdef __EMSCRIPTEN__
// Real elapsed milliseconds for wasm. Mini-game hosts (Douyin/WeChat) serve performance.now()
// through weapp-adapter.js, which rescales tt.getPerformance().now() by /1000 and over-reports the
// elapsed time (observed ~2x on device), so emscripten_get_now() cannot be trusted for dt: it
// inflates the measured frame time and speeds gameplay up. std::chrono::system_clock maps to the
// host's real wall clock (JS Date.now, epoch ms), which is not rescaled, so use it instead.
static double RealNowMs()
{
    using namespace std::chrono;
    return static_cast<double>(duration_cast<microseconds>(system_clock::now().time_since_epoch()).count()) * 0.001;
}
#endif

static unsigned Tick()
{
#ifdef __EMSCRIPTEN__
    static const double baseValue = RealNowMs();
    return static_cast<unsigned>(RealNowMs() - baseValue);
#else
    return SDL_GetTicks();
#endif
}

static long long HiresTick()
{
#ifdef _WIN32
#ifndef UWP
    if (HiresTimer::IsSupported())
    {
#endif
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        return counter.QuadPart;
#ifndef UWP
    }
    else
        return SDL_GetTicks();
#endif
#elif __EMSCRIPTEN__
    return (long long)(RealNowMs() * 1000.0);
#else
    struct timeval time{};
    gettimeofday(&time, nullptr);
    return time.tv_sec * 1000000LL + time.tv_usec;
#endif
}

void Time::BeginFrame(float timeStep)
{
    ++frameNumber_;
    if (!frameNumber_)
        ++frameNumber_;

    timeStep_ = timeStep;
    isFrameInProgress_ = true;

    // Measure the real presented frame rate over a fixed wall-clock window. This deliberately does
    // NOT derive from timeStep_: GetFramesPerSecond used to be 1/timeStep_, but timeStep_ is the
    // smoothed and min/max-fps-clamped logical timestep, so 1/dt over-reports whenever the clamp is
    // active - e.g. a wasm main loop throttled to 30Hz with minFps=30 truncates every slow frame's dt
    // to 1/30, pinning the reading at a constant 30 even when the device actually stalls to 20.
    // Counting frames against a real stopwatch (not the logical timestep) yields the honest rate that
    // DebugHud and gameplay HUDs should show.
    ++fpsWindowFrames_;
    const unsigned windowMs = fpsWindowTimer_.GetMSec(false);
    if (windowMs >= FPS_WINDOW_MS)
    {
        framesPerSecond_ = fpsWindowFrames_ * 1000.0f / static_cast<float>(windowMs);
        fpsWindowFrames_ = 0;
        fpsWindowTimer_.Reset();
    }

    {
        URHO3D_PROFILE("BeginFrame");

        // Frame begin event
        using namespace BeginFrame;

        VariantMap& eventData = GetEventDataMap();
        eventData[P_FRAMENUMBER] = frameNumber_;
        eventData[P_TIMESTEP] = timeStep_;
        SendEvent(E_BEGINFRAME, eventData);
    }
}

void Time::EndFrame()
{
    {
        URHO3D_PROFILE("EndFrame");

        // Frame end event
        SendEvent(E_ENDFRAME);

        // Internal frame end event used only by the engine/tools
        SendEvent(E_ENDFRAMEPRIVATE);
    }

    isFrameInProgress_ = false;
}

void Time::SetTimerPeriod(unsigned mSec)
{
#if defined(_WIN32) && !defined(UWP)
    if (timerPeriod_ > 0)
        timeEndPeriod(timerPeriod_);

    timerPeriod_ = mSec;

    if (timerPeriod_ > 0)
        timeBeginPeriod(timerPeriod_);
#endif
}

float Time::GetElapsedTime()
{
    return elapsedTime_.GetMSec(false) / 1000.0f;
}

unsigned Time::GetSystemTime()
{
    return Tick();
}

unsigned Time::GetTimeSinceEpoch()
{
    return (unsigned)time(nullptr);
}

ea::string Time::GetTimeStamp(const char* format)
{
    time_t timestamp = 0;
    time(&timestamp);
    return GetTimeStamp(timestamp, format);
}

ea::string Time::GetTimeStamp(time_t timestamp, const char* format)
{
    if (format == nullptr)
        format = DEFAULT_DATE_TIME_FORMAT;

    char dateTime[128];
    tm* timeInfo = localtime(&timestamp);
    strftime(dateTime, sizeof(dateTime), format, timeInfo);
    return dateTime;
}

void Time::Sleep(unsigned mSec)
{
#ifdef _WIN32
    ::Sleep(mSec);
#else
    timespec time{static_cast<time_t>(mSec / 1000), static_cast<long>((mSec % 1000) * 1000000)};
    nanosleep(&time, nullptr);
#endif
}

float Time::GetFramesPerSecond() const
{
    // Measured over a real wall-clock window in BeginFrame (see fpsWindowTimer_). Deliberately not
    // 1.0f / timeStep_: the logical timestep is clamped by min/max fps and would over-report under
    // throttling. Returns 0 only until the first measurement window completes.
    return framesPerSecond_;
}

Timer::Timer()
{
    Reset();
}

unsigned Timer::GetMSec(bool reset)
{
    unsigned currentTime = Tick();
    unsigned elapsedTime = currentTime - startTime_;
    if (reset)
        startTime_ = currentTime;

    return elapsedTime;
}

void Timer::Reset()
{
    startTime_ = Tick();
}

HiresTimer::HiresTimer()
{
    Reset();
}

long long HiresTimer::GetUSec(bool reset)
{
    long long currentTime = HiresTick();
    long long elapsedTime = currentTime - startTime_;

    // Correct for possible weirdness with changing internal frequency
    if (elapsedTime < 0)
        elapsedTime = 0;

    if (reset)
        startTime_ = currentTime;

    return (elapsedTime * 1000000LL) / frequency;
}

void HiresTimer::Reset()
{
    startTime_ = HiresTick();
}

TimedCounter::TimedCounter(int intervalCount, unsigned intervalLengthMs)
    : intervalLength_(intervalLengthMs)
    , intervalCurrent_(intervalCount - 1)
{
    data_.resize(intervalCount);
}

void TimedCounter::AddSample(float value)
{
    RotateIfNeeded();
    accumulator_ += value;
}

float TimedCounter::GetAverage()
{
    RotateIfNeeded();
    float sum = 0.0f;
    for (float value : data_)
        sum += value;
    return sum / data_.size();
}

float TimedCounter::GetLast()
{
    RotateIfNeeded();
    return data_[intervalCurrent_];
}

void TimedCounter::RotateIfNeeded()
{
    if (timer_.GetMSec(false) >= intervalLength_)
    {
        timer_.Reset();
        intervalCurrent_ = (intervalCurrent_ + 1) % data_.size();
        data_[intervalCurrent_] = accumulator_;
        accumulator_ = 0.0f;
    }
}

}
