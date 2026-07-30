/* ==========================================================================
   Developer Interactive CLI Terminal Overlay
   ========================================================================== */

document.addEventListener('DOMContentLoaded', () => {
  const terminalDrawer = document.getElementById('terminalDrawer');
  const terminalBar = document.getElementById('terminalBar');
  const terminalBody = document.getElementById('terminalBody');
  const terminalInput = document.getElementById('terminalInput');

  if (!terminalDrawer || !terminalBar || !terminalInput) return;

  // Toggle collapse/expand terminal drawer
  terminalBar.addEventListener('click', () => {
    terminalDrawer.classList.toggle('collapsed');
  });

  const commands = {
    help: `Available commands:
  - <b style="color:var(--cyan);">bench</b> : Run live IronAgent benchmark telemetry
  - <b style="color:var(--cyan);">ironagent</b> : Display IronAgent system specifications
  - <b style="color:var(--cyan);">sysinfo</b> : Print developer system profile & skills
  - <b style="color:var(--cyan);">projects</b> : List all engineering builds
  - <b style="color:var(--cyan);">clear</b> : Clear terminal screen`,

    bench: `[IRONAGENT BENCHMARK EXECUTING...]
  Hot-Loop Throughput : <span style="color:var(--emerald);">272,487 iterations/sec</span>
  50,000 Context Steps: <span style="color:var(--emerald);">0.183 seconds</span>
  Memory RSS Footprint: <span style="color:var(--cyan);">14.0 MB</span>
  SIMD Dot-Product    : <span style="color:var(--amber);">1.2 µs (AVX2/NEON)</span>
  Telemetry Latency   : <span style="color:var(--emerald);">&lt; 50ns (Lockless SPSC)</span>
  STATUS              : <span style="color:var(--emerald);">PASS (50x faster than LangChain)</span>`,

    ironagent: `[IRONAGENT CORE SPECIFICATION]
  Language Core : C11 (Bare-Metal Arenas) & C++17 (Reasoning Engine)
  FFI Protocol  : Pybind11 Zero-Copy memoryview
  Swarm Engine  : C++ Async Fan-Out/Fan-In (SwarmOrchestrator)
  RAG Vector    : Embedded SIMD (AVX2 / NEON intrinsics)
  Shared Memory : POSIX pthread_rwlock_t (ca_shm_t)
  State Machine : Think -> Plan -> Act -> Reflect`,

    sysinfo: `[DEVELOPER PROFILE]
  Name     : Arun Kumar
  Role     : Software Systems & AI Engineer
  Location : Thanjavur, Tamil Nadu, India
  Languages: C++ (17/20/23), C (11), Python 3, Embedded C, CUDA C++
  Domain   : Systems Software, Multithreading, OS Internals, AI Runtimes`,

    projects: `1. IronAgent (Bare-Metal AI Agent Runtime)
2. Distributed Key-Value Store (C++17, TCP consensus, slab allocators)
3. Zero-Copy RPC Framework (Binary protocol 12x faster than JSON)
4. Automotive CAN Bus BCM (Vector India 2024 - Multi-node arbitration)
5. STM32 Bare-Metal Battleship Game Engine (Hardware RNG AI, ILI9341 LCD)`
  };

  function appendLine(html, type = 'output') {
    const line = document.createElement('div');
    line.className = `terminal-line ${type}`;
    line.innerHTML = html;
    terminalBody.insertBefore(line, terminalInput.closest('.terminal-input-line'));
    terminalBody.scrollTop = terminalBody.scrollHeight;
  }

  terminalInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
      const val = terminalInput.value.trim().toLowerCase();
      if (!val) return;

      appendLine(`<span class="terminal-prompt">$</span> ${val}`, 'input');
      terminalInput.value = '';

      if (val === 'clear') {
        const lines = terminalBody.querySelectorAll('.terminal-line');
        lines.forEach(l => l.remove());
        return;
      }

      if (commands[val]) {
        appendLine(commands[val]);
      } else {
        appendLine(`Command not found: '${val}'. Type <b style="color:var(--cyan);">help</b> for command list.`);
      }
    }
  });
});
