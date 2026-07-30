/* ==========================================================================
   Interactive Benchmarks Visualizer Engine for IronAgent
   ========================================================================== */

const benchmarkData = {
  throughput: {
    title: "Hot-Loop State Machine Throughput",
    unit: "iterations/sec (Higher is Better)",
    items: [
      { name: "IronAgent (C11/C++17 Core)", value: 272487, display: "272,487 ops/s", fillClass: "ironagent", widthPct: 100 },
      { name: "OpenAI Agents SDK (Python)", value: 6200, display: "6,200 ops/s", fillClass: "competitor", widthPct: 5 },
      { name: "AutoGen (Python Async)", value: 5120, display: "5,120 ops/s", fillClass: "competitor", widthPct: 4 },
      { name: "CrewAI Engine (Pydantic)", value: 3800, display: "3,800 ops/s", fillClass: "competitor", widthPct: 3 },
      { name: "LangChain (Python Heavy)", value: 2500, display: "2,500 ops/s", fillClass: "competitor", widthPct: 2 }
    ],
    note: "IronAgent achieves ~50x to 100x higher state-transition throughput by eliminating Python object instantiations and executing the Think-Plan-Act loop in native compiled C++."
  },
  contextTime: {
    title: "50,000 Context Steps Execution Time",
    unit: "Seconds (Lower is Better)",
    items: [
      { name: "IronAgent (O(1) Context Window)", value: 0.183, display: "0.183s", fillClass: "ironagent", widthPct: 3 },
      { name: "OpenAI Agents SDK", value: 6.8, display: "6.80s", fillClass: "competitor", widthPct: 37 },
      { name: "AutoGen Context Sliding", value: 8.4, display: "8.40s", fillClass: "competitor", widthPct: 46 },
      { name: "CrewAI Validation Engine", value: 12.5, display: "12.50s", fillClass: "competitor", widthPct: 68 },
      { name: "LangChain Context Window", value: 18.2, display: "18.20s", fillClass: "competitor", widthPct: 100 }
    ],
    note: "O(1) context sliding-window with semispace ping-pong arena compaction reduces execution time by over 99% compared to traditional Python array slicing."
  },
  memoryRss: {
    title: "Memory Footprint (RSS)",
    unit: "Megabytes (Lower is Better)",
    items: [
      { name: "IronAgent (32-byte Bump Arena)", value: 14.0, display: "14.0 MB", fillClass: "ironagent", widthPct: 4 },
      { name: "OpenAI Agents SDK Base", value: 140.0, display: "140.0 MB", fillClass: "competitor", widthPct: 38 },
      { name: "AutoGen Swarm Agent", value: 185.0, display: "185.0 MB", fillClass: "competitor", widthPct: 50 },
      { name: "CrewAI Memory Base", value: 240.0, display: "240.0 MB", fillClass: "competitor", widthPct: 65 },
      { name: "LangChain Framework Base", value: 370.0, display: "370.0 MB", fillClass: "competitor", widthPct: 100 }
    ],
    note: "Zero-GC memory arenas (ca_arena_t) keep the entire engine RSS footprint under 16 MB, making it ideal for edge servers and multi-instance swarms."
  },
  simdVector: {
    title: "Vector Dot-Product Top-K RAG Search (100k Vectors)",
    unit: "Microseconds (Lower is Better)",
    items: [
      { name: "IronAgent VectorBuffer (AVX2 / NEON SIMD)", value: 1.2, display: "1.2 µs", fillClass: "ironagent", widthPct: 3 },
      { name: "NumPy C-API Sliced Search", value: 24.5, display: "24.5 µs", fillClass: "competitor", widthPct: 50 },
      { name: "Pure Python Vector Loop", value: 48.0, display: "48.0 µs", fillClass: "competitor", widthPct: 100 }
    ],
    note: "Hardware-native AVX2 / ARM NEON vector SIMD intrinsics perform microsecond top-k context retrieval without third-party vector DB dependencies."
  }
};

function renderBenchmark(metricKey) {
  const data = benchmarkData[metricKey];
  if (!data) return;

  const chartContainer = document.getElementById('benchmarkChart');
  const noteContainer = document.getElementById('benchmarkNote');
  const titleContainer = document.getElementById('benchmarkMetricTitle');

  if (titleContainer) {
    titleContainer.textContent = `${data.title} — ${data.unit}`;
  }

  if (chartContainer) {
    chartContainer.innerHTML = data.items.map(item => `
      <div class="bar-group">
        <div class="bar-label">
          <span>${item.name}</span>
          <span style="color: ${item.fillClass === 'ironagent' ? 'var(--cyan)' : 'var(--text-muted)'}">${item.display}</span>
        </div>
        <div class="bar-track">
          <div class="bar-fill ${item.fillClass}" style="width: ${Math.max(item.widthPct, 4)}%">
            ${item.display}
          </div>
        </div>
      </div>
    `).join('');
  }

  if (noteContainer) {
    noteContainer.textContent = data.note;
  }
}

document.addEventListener('DOMContentLoaded', () => {
  renderBenchmark('throughput');

  const benchBtns = document.querySelectorAll('.bench-btn');
  benchBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      benchBtns.forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      const metric = btn.getAttribute('data-metric');
      renderBenchmark(metric);
    });
  });
});
