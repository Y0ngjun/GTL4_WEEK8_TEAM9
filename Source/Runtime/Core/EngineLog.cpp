#include "EnginePCH.h"
#include "EngineLog.h"

namespace
{
    TArray<ILogSink*>& GetSinks()
    {
        static TArray<ILogSink*> Sinks;
        return Sinks;
    }
}

void FLog::AddSink(ILogSink* Sink)
{
    if (Sink)
        GetSinks().Add(Sink);
}

void FLog::RemoveSink(ILogSink* Sink)
{
    TArray<ILogSink*>& Sinks = GetSinks();
    for (int32 Index = Sinks.Num() - 1; Index >= 0; --Index)
        if (Sinks[Index] == Sink)
            Sinks.RemoveAt(Index, 1);
}

void FLog::Emit(ELogVerbosity Verbosity, const FString& Message)
{
    // 편집기 로그 패널 생성 전 시작 실패도 디버거에서 확인할 수 있게 한다.
    if (GetSinks().IsEmpty())
    {
        OutputDebugStringA(Message.c_str());
        OutputDebugStringA("\n");
    }
    for (ILogSink* Sink : GetSinks())
        Sink->OnLog(Verbosity, Message);
}