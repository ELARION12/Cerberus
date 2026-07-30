/* ==========================================================================
   Interactive System Architecture Pipeline Explorer
   ========================================================================== */

const architecturePipelineData = {
  swarm: {
    title: "SwarmOrchestrator Module",
    subtitle: "Hierarchical C++ Async Fan-Out / Fan-In Worker Engine",
    col1Title: "Execution & Threading Pipeline",
    col1Bullets: [
      "Submits parallel worker tasks array to native C pthread pool (ca_thread_pool_t)",
      "Bypasses Python Global Interpreter Lock (GIL) locks entirely during hot-loop execution",
      "Executes subtasks asynchronously using std::packaged_task and std::future primitives"
    ],
    col2Title: "Resilience & System Guarantees",
    col2Bullets: [
      "Enforces per-worker execution timeouts with automatic task cancellation",
      "Isolates worker exceptions to prevent main state machine crashes",
      "Aggregates multi-agent thread results into a unified thread-safe SwarmSummary channel"
    ]
  },
  vector: {
    title: "VectorBuffer Memory Module",
    subtitle: "AVX2 & ARM NEON Hardware SIMD Vector RAG Memory",
    col1Title: "SIMD Acceleration Pipeline",
    col1Bullets: [
      "Hardware 256-bit AVX2 SIMD intrinsics (_mm256_fmadd_ps) with scalar fallbacks",
      "Computes L2-normalised float vector dot-products in under 1.2 microseconds",
      "Direct in-memory zero-copy document embedding storage"
    ],
    col2Title: "Zero-Dependency Local Retrieval",
    col2Bullets: [
      "Performs instant cosine similarity top-k search without external vector databases",
      "Directly injects top-k prompt context into ContextBuffer semispace arenas",
      "Zero network IPC latency penalty during local RAG inference"
    ]
  },
  parser: {
    title: "OutputParser AST Classifier",
    subtitle: "Zero-Allocation C++ JSON & POSIX ERE Regex Parser",
    col1Title: "Zero-Heap Token Extraction",
    col1Bullets: [
      "Executes directly inside Agent::reflect() state machine transition loop",
      "Extracted string views point directly into bump arena memory pointers",
      "Completely eliminates Python string allocation and GC pressure"
    ],
    col2Title: "Automated Format Classification",
    col2Bullets: [
      "Detects JSON_OBJECT, JSON_ARRAY, SCALAR, and PLAIN_TEXT output structures",
      "Evaluates POSIX Extended Regular Expression (ERE) schema compliance",
      "Passes validated structured payloads directly into next planning iteration"
    ]
  },
  telemetry: {
    title: "ca_ringbuf_t Telemetry Engine",
    subtitle: "C11 Single-Producer Single-Consumer Lockless Ring Buffer",
    col1Title: "Lock-Free Ring Mechanics",
    col1Bullets: [
      "Atomic head and tail cursors aligned to 64-byte CPU cache lines",
      "Prevents false sharing and thread contention on multi-core CPUs",
      "Nanosecond event recording latency (<50ns per telemetry event)"
    ],
    col2Title: "OpenTelemetry Integration",
    col2Bullets: [
      "Monotonic nanosecond timestamping via C core ca_now_ns()",
      "Direct export to OpenTelemetry (OTLP) standard NDJSON log format",
      "Zero-allocation event ring buffer serialization"
    ]
  },
  arena: {
    title: "ca_arena_t Memory Allocator",
    subtitle: "Bare-Metal C11 32-Byte SIMD Bump Pointer Arena",
    col1Title: "Allocation & Alignment Specs",
    col1Bullets: [
      "Ultra-fast bump pointer allocation executed in ~2 nanoseconds",
      "Strict 32-byte memory alignment for AVX2 vector SIMD register safety",
      "Pre-allocated contiguous memory blocks preventing OS kernel page faults"
    ],
    col2Title: "O(1) Context Pruning & Compaction",
    col2Bullets: [
      "Semispace ping-pong garbage collection (retained_ and standby_ arenas)",
      "Context window sliding pruning completed in under 0.5 milliseconds",
      "Sustains zero fragmentation across millions of state machine transitions"
    ]
  }
};

function renderArchitecturePipeline(key) {
  const data = architecturePipelineData[key];
  if (!data) return;

  const titleEl = document.getElementById('pipelineTitle');
  const subtitleEl = document.getElementById('pipelineSubtitle');
  const col1Title = document.getElementById('col1Title');
  const col1Bullets = document.getElementById('col1Bullets');
  const col2Title = document.getElementById('col2Title');
  const col2Bullets = document.getElementById('col2Bullets');

  if (titleEl) titleEl.textContent = data.title;
  if (subtitleEl) subtitleEl.textContent = data.subtitle;
  if (col1Title) col1Title.textContent = data.col1Title;
  if (col2Title) col2Title.textContent = data.col2Title;

  if (col1Bullets) {
    col1Bullets.innerHTML = data.col1Bullets.map(b => `
      <li><span class="bullet">✓</span> <span>${b}</span></li>
    `).join('');
  }

  if (col2Bullets) {
    col2Bullets.innerHTML = data.col2Bullets.map(b => `
      <li><span class="bullet">✓</span> <span>${b}</span></li>
    `).join('');
  }
}

document.addEventListener('DOMContentLoaded', () => {
  renderArchitecturePipeline('swarm');

  const archTabs = document.querySelectorAll('.arch-tab');
  archTabs.forEach(tab => {
    tab.addEventListener('click', () => {
      archTabs.forEach(t => t.classList.remove('active'));
      tab.classList.add('active');
      const key = tab.getAttribute('data-arch');
      renderArchitecturePipeline(key);
    });
  });
});
