/* kernel/include/onnx_stub_api.h - Mock ORT API */
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace OrtStub {

class Env {
public:
    Env();
    ~Env();
};

class Session {
public:
    Session(Env& env, const char* model_path, void* options);
    void Run();
};

} /* namespace OrtStub */
