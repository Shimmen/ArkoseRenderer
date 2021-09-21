#pragma once

#include "backend/CommandList.h"

class ScopedDebugZone {
public:
    ScopedDebugZone(CommandList& commandList, const std::string& zoneName)
        : m_commandList(commandList)
    {
        m_commandList.beginDebugLabel(zoneName);
    }

    ~ScopedDebugZone()
    {
        m_commandList.endDebugLabel();
    }

    ScopedDebugZone(ScopedDebugZone&) = delete;
    ScopedDebugZone(ScopedDebugZone&&) = delete;
    ScopedDebugZone& operator=(const ScopedDebugZone&) = delete;

private:
    CommandList& m_commandList;
};