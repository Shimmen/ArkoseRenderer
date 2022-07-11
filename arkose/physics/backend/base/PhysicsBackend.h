#pragma once

class PhysicsBackend {
private:
    // Only one physics backend can exist at any point in time
    static PhysicsBackend* s_globalPhysicsBackend;

public:

    enum class Type {
        None,
        Jolt,
    };

    static PhysicsBackend* create(PhysicsBackend::Type);
    static void destroy();

protected:

    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

};
