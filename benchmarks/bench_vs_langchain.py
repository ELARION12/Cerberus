# benchmarks/bench_vs_langchain.py
import os
import sys
import time
import resource
import gc

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../python"))

def get_memory_mb():
    # RUSAGE_SELF returns kilobytes on Linux
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024.0

def measure_coreagent():
    print("=== CoreAgent Orchestration (Isolated) ===", flush=True)
    time.sleep(1.5)  # Pause for GIF recording

    gc.collect()
    mem_start = get_memory_mb()
    
    # --- START BENCHMARK TIMER ---
    start_time = time.perf_counter()

    from coreagent.agent import Agent
    from coreagent._coreagent_cpp import AgentConfig

    config = AgentConfig()
    config.context_arena_capacity = 1024 * 1024    
    config.context_scratch_capacity = 1024 * 1024  
    
    agent = Agent(config=config)
    agent.context.add_system("You are a high-performance bare-metal orchestrated agent.")

    for i in range(1000):
        agent.context.add_user(f"Data payload {i}: " + "x" * 150)
        agent.context.add_assistant(f"Processed payload {i}.")

    prompt_view = agent.context.build_prompt()
    prompt = bytes(prompt_view).decode('utf-8')

    end_time = time.perf_counter()
    # --- END BENCHMARK TIMER ---
    
    gc.collect()
    mem_end = get_memory_mb()

    print(f"Time-to-Ready (TTFA) : {(end_time - start_time) * 1000:.3f} ms", flush=True)
    print(f"Memory Footprint (RSS): {mem_end - mem_start:.2f} MB", flush=True)
    print(f"Final Prompt Output   : {len(prompt)} bytes", flush=True)
    print("==========================================\n", flush=True)
    
    time.sleep(2.0)  # Pause to let the viewer digest CoreAgent's score

def measure_langchain():
    print("=== LangChain Orchestration (Isolated) ===", flush=True)
    time.sleep(1.5)  # Pause for GIF recording

    gc.collect()
    mem_start = get_memory_mb()
    
    # --- START BENCHMARK TIMER ---
    start_time = time.perf_counter()

    try:
        from langchain_core.messages import SystemMessage, HumanMessage, AIMessage
    except ImportError:
        print("[LangChain is not installed in the environment. Skipping LangChain benchmark.]\n", flush=True)
        return

    messages = [SystemMessage(content="You are a high-performance bare-metal orchestrated agent.")]

    for i in range(1000):
        messages.append(HumanMessage(content=f"Data payload {i}: " + "x" * 150))
        messages.append(AIMessage(content=f"Processed payload {i}."))

    current_chars = sum(len(m.content) for m in messages)
    while current_chars > 16384 and len(messages) > 1:
        dropped = messages.pop(1)
        current_chars -= len(dropped.content)

    prompt = "\n".join([f"[{m.type.upper()}]: {m.content}" for m in messages])

    end_time = time.perf_counter()
    # --- END BENCHMARK TIMER ---
    
    gc.collect()
    mem_end = get_memory_mb()

    print(f"Time-to-Ready (TTFA) : {(end_time - start_time) * 1000:.3f} ms", flush=True)
    print(f"Memory Footprint (RSS): {mem_end - mem_start:.2f} MB", flush=True)
    print(f"Final Prompt Output   : {len(prompt)} bytes", flush=True)
    print("==========================================\n", flush=True)
    
    time.sleep(3.0)  # Hold final output before loop repeats

if __name__ == "__main__":
    if len(sys.argv) == 1:
        import subprocess
        subprocess.run([sys.executable, sys.argv[0], "--coreagent"])
        subprocess.run([sys.executable, sys.argv[0], "--langchain"])
    elif sys.argv[1] == "--coreagent":
        measure_coreagent()
    elif sys.argv[1] == "--langchain":
        measure_langchain()