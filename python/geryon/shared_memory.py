"""
Thread-safe key-value blackboard shared across multiple Agent instances.
Backed by a C++ arena guarded by a pthread read-write lock, so several readers can read at once.
"""

from typing import Any, Dict
try:
    from ._geryon_core import SharedMemory as _CppSharedMemory
except ImportError:
    _CppSharedMemory = None


class SharedMemory:
    """Key-value blackboard shared across a team of agents."""

    def __init__(self, capacity: int = 2 * 1024 * 1024):
        if _CppSharedMemory is not None:
            self._impl = _CppSharedMemory(capacity)
        else:
            self._impl = None
            self._fallback: Dict[str, str] = {}

    def write(self, key: str, value: Any, by: str = "agent") -> None:
        str_val = str(value)
        if self._impl is not None:
            self._impl.write(key, str_val, by)
        else:
            self._fallback[key] = str_val

    def read(self, key: str, default: Any = None) -> Any:
        if self._impl is not None:
            res = self._impl.read(key, "")
            return res if res else default
        return self._fallback.get(key, default)

    def summary(self) -> str:
        """Human-readable dump injected into an agent's prompt."""
        if self._impl is not None:
            return self._impl.summary()
        if not self._fallback:
            return ""
        return "\n".join(f"- {k} = {v}" for k, v in self._fallback.items())

    def count(self) -> int:
        if self._impl is not None:
            return self._impl.count()
        return len(self._fallback)

    @property
    def raw_cpp(self):
        return self._impl
