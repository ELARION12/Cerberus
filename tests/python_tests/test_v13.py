"""
V1.3.0 Feature Tests (VectorBuffer, Telemetry, Swarm)
"""

import sys
import os
import pytest

sys.path.insert(0, os.path.join(
    os.path.dirname(__file__), "../../python"
))

from cerberus import (
    VectorBuffer, Telemetry, SwarmResult, SwarmSummary, version
)
from cerberus._coreagent_cpp import TelemetryKind


def test_v13_version():
    assert version() == "1.4.0"
    print("\n✅ V1.3.0 Version PASSED")


def test_v13_vector_buffer():
    vb = VectorBuffer(dim=4, max_vectors=1024)
    assert vb.dim() == 4
    assert vb.empty()

    idx1 = vb.push([1.0, 0.0, 0.0, 0.0], "Text A")
    idx2 = vb.push([0.0, 1.0, 0.0, 0.0], "Text B")
    assert vb.size() == 2
    assert vb.text(idx1) == "Text A"

    matches = vb.query([0.9, 0.1, 0.0, 0.0], top_k=1)
    assert len(matches) == 1
    assert matches[0].text == "Text A"
    assert matches[0].score > 0.8

    rag = vb.rag_context([0.9, 0.1, 0.0, 0.0], top_k=1)
    assert "Text A" in rag
    print("\n✅ V1.3.0 VectorBuffer PASSED")


def test_v13_telemetry():
    t = Telemetry(capacity=512)
    assert t.available() == 0
    assert not t.dropped()

    t.emit(TelemetryKind.THINK, "AgentX", "thinking")
    t.emit(TelemetryKind.ACT,   "AgentX", "acting")
    assert t.available() == 2
    print("\n✅ V1.3.0 Telemetry PASSED")


def test_v13_swarm_structs():
    r = SwarmResult()
    r.worker_name = "WorkerA"
    r.output = "OutputA"
    r.success = True
    r.elapsed_ms = 12

    s = SwarmSummary()
    s.results = [r]
    s.succeeded = 1
    s.failed = 0
    s.total_elapsed_ms = 12

    combined = s.combined()
    assert "[WorkerA]: OutputA" in combined
    print("\n✅ V1.3.0 Swarm Structs PASSED")
