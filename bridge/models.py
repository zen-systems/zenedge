import torch
import torch.nn as nn
from pathlib import Path
from typing import Dict, Optional


class ModelCache:
    """
    Caches PyTorch models for efficient repeated inference.

    Models are loaded from .pt files (TorchScript format) or created
    dynamically if no file exists.
    """

    def __init__(self, model_dir: str = "./models"):
        self.model_dir = Path(model_dir)
        self.cache: Dict[str, nn.Module] = {}
        self.device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

        # Create model directory if needed
        self.model_dir.mkdir(parents=True, exist_ok=True)

        print(f"[MODELS] Initialized with device: {self.device}")
        print(f"[MODELS] Model directory: {self.model_dir.absolute()}")

    def get_or_load(self, name: str) -> nn.Module:
        """
        Get a model by name, loading from disk or creating default if needed.

        Args:
            name: Model name (without .pt extension)

        Returns:
            PyTorch module ready for inference
        """
        if name in self.cache:
            return self.cache[name]

        model_path = self.model_dir / f"{name}.pt"

        if model_path.exists():
            print(f"[MODELS] Loading model from {model_path}")
            try:
                model = torch.jit.load(str(model_path), map_location=self.device)
            except Exception as e:
                print(f"[MODELS] Failed to load TorchScript model: {e}")
                print("[MODELS] Trying to load as state_dict...")
                # Try loading as state dict
                state_dict = torch.load(str(model_path), map_location=self.device)
                model = self._create_model_from_state_dict(name, state_dict)
        else:
            print(f"[MODELS] Model '{name}' not found, creating default")
            model = self._create_default_model(name)

        model = model.to(self.device)
        model.eval()
        self.cache[name] = model

        return model

    def _create_default_model(self, name: str) -> nn.Module:
        """
        Create a default model for testing when no .pt file exists.

        Different default models based on name:
        - "default": Simple MLP for MNIST-like data
        - "linear": Single linear layer
        - "identity": Pass-through (returns input)
        """
        if name == "identity":
            return IdentityModel()
        elif name == "linear":
            return nn.Linear(784, 10)
        elif name == "sum":
            return SumModel()
        elif name == "mean":
            return MeanModel()
        else:
            # Default: simple MLP
            return nn.Sequential(
                nn.Flatten(),
                nn.Linear(784, 128),
                nn.ReLU(),
                nn.Linear(128, 10),
            )

    def _create_model_from_state_dict(self, name: str, state_dict: dict) -> nn.Module:
        """Attempt to infer model architecture from state dict."""
        # This is a simplified version - in practice you'd need more logic
        # or store architecture info alongside weights
        print(f"[MODELS] Warning: Creating model from state_dict is experimental")

        # Try to infer a simple linear model
        keys = list(state_dict.keys())
        if len(keys) == 2 and 'weight' in keys[0] and 'bias' in keys[1]:
            weight = state_dict[keys[0]]
            model = nn.Linear(weight.shape[1], weight.shape[0])
            model.load_state_dict(state_dict)
            return model

        raise ValueError(f"Cannot infer model architecture for '{name}'")

    def preload(self, names: list):
        """Preload multiple models into cache."""
        for name in names:
            try:
                self.get_or_load(name)
            except Exception as e:
                print(f"[MODELS] Failed to preload '{name}': {e}")

    def clear_cache(self):
        """Clear all cached models."""
        self.cache.clear()
        torch.cuda.empty_cache() if torch.cuda.is_available() else None

    def list_available(self) -> list:
        """List available .pt files in model directory."""
        return [p.stem for p in self.model_dir.glob("*.pt")]

    def save_model(self, name: str, model: nn.Module, as_torchscript: bool = True):
        """
        Save a model to disk.

        Args:
            name: Model name (without .pt extension)
            model: PyTorch module to save
            as_torchscript: If True, save as TorchScript for portability
        """
        model_path = self.model_dir / f"{name}.pt"

        if as_torchscript:
            # Trace with example input
            model.eval()
            scripted = torch.jit.script(model)
            scripted.save(str(model_path))
        else:
            torch.save(model.state_dict(), str(model_path))

        print(f"[MODELS] Saved model to {model_path}")


class IdentityModel(nn.Module):
    """Pass-through model for testing."""

    def forward(self, x):
        return x


class SumModel(nn.Module):
    """Returns sum of input tensor."""

    def forward(self, x):
        return x.sum(keepdim=True)


class MeanModel(nn.Module):
    """Returns mean of input tensor."""

    def forward(self, x):
        return x.mean(keepdim=True)


def create_test_models(model_dir: str = "./models"):
    """Create and save test models for development."""
    cache = ModelCache(model_dir)

    # Create and save a simple linear model
    linear = nn.Linear(784, 10)
    cache.save_model("linear_784_10", linear, as_torchscript=False)

    # Create and save a simple MLP
    mlp = nn.Sequential(
        nn.Flatten(),
        nn.Linear(784, 128),
        nn.ReLU(),
        nn.Linear(128, 10),
    )
    # For TorchScript, we need to trace with example input
    mlp.eval()
    example_input = torch.randn(1, 1, 28, 28)
    traced = torch.jit.trace(mlp, example_input)
    traced.save(str(cache.model_dir / "mlp_mnist.pt"))

    print("[MODELS] Created test models:")
    print(f"  - linear_784_10.pt")
    print(f"  - mlp_mnist.pt")


if __name__ == "__main__":
    # Create test models when run directly
    create_test_models()
