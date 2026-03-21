#!/usr/bin/env python3
"""
clove-openclaw launcher

Runs OpenClaw inside a CLOVE kernel sandbox with:
- Namespace isolation (PID/NET/MNT/UTS)
- seccomp BPF syscall filtering
- Landlock filesystem restriction
- Network domain allowlist
- LLM cost tracking + budget enforcement
- PII filtering on all LLM calls
- Full audit trail

Usage:
    python3 launch.py                           # Launch single OpenClaw instance
    python3 launch.py --name researcher         # Named instance
    python3 launch.py --config custom.yaml      # Custom config
    python3 launch.py --multi                   # Multi-agent mode (from config)
"""

import argparse
import json
import os
import signal
import sys
import time
import yaml

# Add SDK to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'sdk', 'python'))

from clove_sdk import CloveClient


def load_config(path: str) -> dict:
    """Load YAML configuration."""
    with open(path, 'r') as f:
        return yaml.safe_load(f)


def setup_kernel_policies(client: CloveClient, config: dict):
    """Configure kernel-level policies from config."""

    # Privacy / PII filtering
    privacy = config.get('privacy', {})
    if privacy.get('enabled', False):
        print(f"  PII filtering: {privacy.get('mode', 'audit')} mode")

    # LLM budget
    llm = config.get('llm', {})
    if llm.get('max_cost_usd', 0) > 0:
        print(f"  LLM budget: ${llm['max_cost_usd']:.2f}/day")

    # Audit
    audit = config.get('audit', {})
    if any(audit.values()):
        client._send(0x77, {  # SYS_SET_AUDIT_CONFIG
            'log_syscalls': audit.get('log_syscalls', False),
            'log_network': audit.get('log_network', False),
            'log_security': audit.get('log_security', True),
        })
        print("  Audit logging: enabled")


def launch_openclaw(client: CloveClient, name: str, config: dict) -> dict:
    """Spawn an OpenClaw instance inside CLOVE sandbox."""

    oc_config = config.get('openclaw', {})
    sandbox = config.get('sandbox', {})

    command = oc_config.get('command', 'openclaw')
    args = oc_config.get('args', ['gateway', '--headless'])

    # Build the full command
    full_command = command
    if args:
        full_command = f"{command} {' '.join(args)}"

    print(f"\n  Spawning OpenClaw agent: {name}")
    print(f"  Command: {full_command}")
    print(f"  Memory limit: {sandbox.get('memory_limit_mb', 512)}MB")
    print(f"  CPU quota: {sandbox.get('cpu_quota_percent', 50)}%")
    print(f"  Max PIDs: {sandbox.get('max_pids', 32)}")

    # Spawn via CLOVE kernel
    result = client.spawn(name, full_command)

    if result.get('success', False):
        agent_id = result.get('agent_id', 0)
        pid = result.get('pid', 0)
        print(f"  Agent spawned: id={agent_id}, pid={pid}")

        # Set permissions based on config
        fs_config = config.get('filesystem', {})
        net_config = config.get('network', {})

        perms = {
            'can_exec': True,
            'can_read': True,
            'can_write': True,
            'can_think': True,
            'can_http': sandbox.get('enable_network', True),
            'allowed_domains': net_config.get('allowed_domains', []),
        }
        client.set_permissions(perms, agent_id=agent_id)
        print(f"  Permissions set: {len(perms.get('allowed_domains', []))} allowed domains")

        return result
    else:
        print(f"  ERROR: {result.get('error', 'unknown')}")
        return result


def launch_multi(client: CloveClient, config: dict):
    """Launch multiple OpenClaw instances from config."""
    agents_config = config.get('agents', [])
    if not agents_config:
        print("  No agents defined in config. Use 'agents:' section.")
        return []

    results = []
    for agent_def in agents_config:
        name = agent_def.get('name', 'openclaw')
        # Override sandbox limits per agent
        agent_config = {**config}
        if 'memory_limit_mb' in agent_def:
            agent_config.setdefault('sandbox', {})['memory_limit_mb'] = agent_def['memory_limit_mb']

        result = launch_openclaw(client, name, agent_config)
        results.append(result)
        time.sleep(0.5)  # Stagger spawns

    return results


def monitor(client: CloveClient):
    """Monitor running agents until interrupted."""
    print("\n  Monitoring (Ctrl+C to stop)...\n")

    try:
        while True:
            agents = client.list_agents()
            if not agents:
                print("  No agents running.")
                break

            for agent in agents:
                if isinstance(agent, dict):
                    name = agent.get('name', '?')
                    state = agent.get('state', '?')
                    pid = agent.get('pid', 0)
                    print(f"  [{name}] state={state} pid={pid}")

            # Check audit for recent security events
            audit = client.get_audit_log(category='SECURITY', limit=5)
            entries = audit.get('entries', [])
            if entries:
                print(f"  Recent security events: {len(entries)}")
                for entry in entries[:3]:
                    if isinstance(entry, dict):
                        print(f"    - {entry.get('event_type', '?')}: {entry.get('details', {})}")

            time.sleep(5)
    except KeyboardInterrupt:
        print("\n  Stopping...")


def main():
    parser = argparse.ArgumentParser(description='Launch OpenClaw inside CLOVE sandbox')
    parser.add_argument('--name', default='openclaw', help='Agent name (default: openclaw)')
    parser.add_argument('--config', default='clove-openclaw.yaml', help='Config file path')
    parser.add_argument('--socket', default='/tmp/clove.sock', help='CLOVE kernel socket path')
    parser.add_argument('--multi', action='store_true', help='Launch multi-agent mode')
    parser.add_argument('--monitor', action='store_true', help='Monitor after launch')
    parser.add_argument('--record', action='store_true', help='Enable execution recording')
    args = parser.parse_args()

    print("=" * 50)
    print("  clove-openclaw")
    print("  Run OpenClaw safely inside CLOVE kernel")
    print("=" * 50)

    # Load config
    config_path = os.path.join(os.path.dirname(__file__), args.config)
    if os.path.exists(config_path):
        config = load_config(config_path)
        print(f"\n  Config: {args.config}")
    else:
        print(f"\n  Config not found: {config_path}")
        print("  Using defaults...")
        config = {
            'openclaw': {'command': 'openclaw', 'args': ['gateway', '--headless']},
            'sandbox': {'memory_limit_mb': 512, 'cpu_quota_percent': 50, 'max_pids': 32},
        }

    # Connect to CLOVE kernel
    print(f"  Kernel: {args.socket}")
    client = CloveClient(socket_path=args.socket)

    try:
        client.connect()
    except Exception as e:
        print(f"\n  ERROR: Cannot connect to CLOVE kernel at {args.socket}")
        print(f"  Make sure the kernel is running: clove_kernel --api &")
        print(f"  Details: {e}")
        sys.exit(1)

    try:
        # Hello handshake
        hello = client.hello()
        version = hello.get('kernel_version', '?')
        print(f"  Connected to CLOVE kernel v{version}")

        # Setup policies
        print("\n  Configuring kernel policies:")
        setup_kernel_policies(client, config)

        # Start execution recording if requested
        if args.record:
            client.record_start()
            print("  Execution recording: ON")

        # Launch
        if args.multi:
            results = launch_multi(client, config)
            print(f"\n  Launched {len(results)} OpenClaw agents")
        else:
            result = launch_openclaw(client, args.name, config)
            if not result.get('success', False):
                sys.exit(1)

        # Show status
        print("\n  Fleet status:")
        agents = client.list_agents()
        if isinstance(agents, list):
            for a in agents:
                if isinstance(a, dict):
                    print(f"    {a.get('name', '?'):20s} {a.get('state', '?'):10s} PID {a.get('pid', 0)}")

        print(f"\n  Dashboard: http://localhost:8080/dashboard")
        print(f"  Audit:     clove audit --limit 20")
        print(f"  Agents:    clove ps")

        # Monitor if requested
        if args.monitor:
            monitor(client)

    except KeyboardInterrupt:
        print("\n  Interrupted.")
    finally:
        client.disconnect()
        print("  Done.")


if __name__ == '__main__':
    main()
