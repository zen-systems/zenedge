from __future__ import annotations

try:
    import onnxruntime as ort
except Exception:
    ort = None

try:
    import torch
    import torch.nn as nn
except Exception:
    torch = None
    nn = None
from pathlib import Path
from typing import Dict, Optional
import numpy as np


class ModelCache:
    """
    Caches ONNX Runtime sessions for efficient inference.
    
    Models are loaded from .onnx files.
    """

    def __init__(self, model_dir: str = "./models"):
        self.model_dir = Path(model_dir)
        self.cache: Dict[str, ort.InferenceSession] = {}
        
        self.disabled = ort is None
        if self.disabled:
            self.providers = []
            self.model_dir.mkdir(parents=True, exist_ok=True)
            print("[MODELS] onnxruntime not available; ModelCache disabled.")
            return

        # Check available providers
        self.providers = ['CPUExecutionProvider']
        if 'CUDAExecutionProvider' in ort.get_available_providers():
            self.providers.insert(0, 'CUDAExecutionProvider')

        # Create model directory if needed
        self.model_dir.mkdir(parents=True, exist_ok=True)

        print(f"[MODELS] Initialized using providers: {self.providers}")
        print(f"[MODELS] Model directory: {self.model_dir.absolute()}")

    def get_or_load(self, name: str) -> ort.InferenceSession:
        """
        Get an ORT session by name, loading from .onnx file or creating default.
        
        Args:
            name: Model name (without .onnx extension)
            
        Returns:
            ONNX Runtime InferenceSession
        """
        if self.disabled:
            raise RuntimeError("ModelCache disabled (onnxruntime not available)")

        if name in self.cache:
            return self.cache[name]

        model_path = self.model_dir / f"{name}.onnx"

        if not model_path.exists():
            print(f"[MODELS] Model '{name}' not found, creating default test model")
            self._create_default_model(name)

        print(f"[MODELS] Loading ORT session from {model_path}")
        try:
            session = ort.InferenceSession(str(model_path), providers=self.providers)
            self.cache[name] = session
            return session
        except Exception as e:
            print(f"[MODELS] Failed to load ONNX model: {e}")
            raise e

    def _create_default_model(self, name: str):
        """
        Create (export) a default ONNX model for testing.
        """
        if torch is None or nn is None:
            raise RuntimeError("Torch not available; cannot create default model")

        if name == "identity":
            model = IdentityModel()
            shape = (1, 32) # Arbitrary
        elif name == "linear":
            model = nn.Linear(784, 10)
            shape = (1, 784)
        else:
            # Default MLP (MNIST-like)
            model = nn.Sequential(
                nn.Flatten(),
                nn.Linear(784, 128),
                nn.ReLU(),
                nn.Linear(128, 10),
            )
            shape = (1, 1, 28, 28)

        # Export to ONNX
        model.eval()
        dummy_input = torch.randn(*shape)
        output_path = self.model_dir / f"{name}.onnx"
        
        torch.onnx.export(
            model, 
            dummy_input, 
            str(output_path),
            input_names=['input'],
            output_names=['output'],
            dynamic_axes={'input': {0: 'batch_size'}, 'output': {0: 'batch_size'}}
        )
        print(f"[MODELS] Exported default model '{name}' to {output_path}")

    def clear_cache(self):
        self.cache.clear()

    def list_available(self) -> list:
        return [p.stem for p in self.model_dir.glob("*.onnx")]


if nn is not None:
    class IdentityModel(nn.Module):
        def forward(self, x):
            return x
else:
    IdentityModel = None


if __name__ == "__main__":
    # Test creation
    cache = ModelCache()
    cache.get_or_load("default")
    cache.get_or_load("linear")
