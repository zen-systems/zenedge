/* kernel/lib/onnx/stub.cpp - Minimal ORT Stub */
#include "onnx_stub_api.h"

namespace OrtStub {

Env::Env() {
    // Stub
}

Env::~Env() {
    // Stub
}

Session::Session(Env& env, const char* model_path, void* options) {
    (void)env; (void)model_path; (void)options;
}

void Session::Run() {
    // Stub
}

} /* namespace OrtStub */
