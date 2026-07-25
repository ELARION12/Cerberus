"""
Simple CLI to run CoreAgent from the terminal.

Usage:
    python -m coreagent "list files in home directory"
    python -m coreagent --name DevBot "check disk space"
"""

import argparse
import sys
from .agent import Agent


def main():
    parser = argparse.ArgumentParser(
        prog="coreagent",
        description="CoreAgent — Bare-Metal AI Agent Runtime"
    )
    parser.add_argument(
        "task",
        nargs="?",
        default=None,
        help="Task for the agent to execute"
    )
    parser.add_argument(
        "--name", default="CoreAgent",
        help="Agent name (default: CoreAgent)"
    )
    parser.add_argument(
        "--steps", type=int, default=10,
        help="Max steps (default: 10)"
    )
    parser.add_argument(
        "--threads", type=int, default=4,
        help="Thread count (default: 4)"
    )
    parser.add_argument(
        "--list-tools", action="store_true",
        help="List all available tools and exit"
    )
    parser.add_argument(
        "--version", action="store_true",
        help="Print version and exit"
    )

    args = parser.parse_args()

    if args.version:
        from . import __version__
        print(f"CoreAgent v{__version__}")
        sys.exit(0)

    agent = Agent(
        name=args.name,
        max_steps=args.steps,
        num_threads=args.threads
    )

    if args.list_tools:
        print(f"\n[{agent.name}] Available tools:")
        for t in agent.tool_names:
            print(f"  • {t}")
        print()
        sys.exit(0)

    if not args.task:
        print("CoreAgent Interactive Mode (type 'exit' to quit)\n")
        while True:
            try:
                task = input(f"[{agent.name}] > ").strip()
                if task.lower() in ("exit", "quit", "q"):
                    break
                if task:
                    result = agent.run(task)
                    print(f"\n📄 Result:\n{result}\n")
            except KeyboardInterrupt:
                print("\nBye! 👋")
                break
    else:
        result = agent.run(args.task)
        print(f"\n📄 Final Result:\n{result}")


if __name__ == "__main__":
    main()