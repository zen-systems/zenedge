# ZENEDGE Python Bridge
# Communicates with ZENEDGE kernel via shared memory IPC

__version__ = "0.1.0"

# Lazy imports to avoid circular dependencies
def get_bridge():
    from .zenedge_bridge import ZenedgeBridge
    return ZenedgeBridge

# Re-export commonly used items
from .protocol import (
    CMD_PING,
    CMD_PRINT,
    CMD_RUN_MODEL,
    RSP_OK,
    RSP_ERROR,
    RSP_BUSY,
)

