"""CLOVE Agent Fleet Kernel — Python SDK"""

from clove_sdk.client import CloveClient
from clove_sdk.agent import Agent
from clove_sdk.protocol import SyscallOp

VERSION = "2.0.0"

__all__ = ["CloveClient", "Agent", "SyscallOp", "VERSION"]
