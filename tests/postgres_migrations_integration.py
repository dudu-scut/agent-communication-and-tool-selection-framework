#!/usr/bin/env python3
"""Exercise the compiled migrator against a real PostgreSQL instance.

The test never treats an unavailable database as a passing fake.  When no
Docker daemon and no complete NEXUSAI_POSTGRES_* test DSN are available it
prints an explicit SKIP and exits successfully so local unit runs remain
hermetic.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time
import uuid


POSTGRES_VARIABLES = (
    "NEXUSAI_POSTGRES_HOST",
    "NEXUSAI_POSTGRES_PORT",
    "NEXUSAI_POSTGRES_DATABASE",
    "NEXUSAI_POSTGRES_USER",
    "NEXUSAI_POSTGRES_PASSWORD",
)


def run(command: list[str], *, env: dict[str, str] | None = None,
        check: bool = True, timeout: int = 60) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        env=env,
        check=check,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
    )


def skip(reason: str) -> int:
    print(f"SKIP: {reason}")
    return 0


def docker_dsn(docker: str, container: str) -> tuple[dict[str, str], str]:
    password = "integration-test-secret"
    run([
        docker, "run", "-d", "--rm", "--name", container,
        "-e", "POSTGRES_DB=nexusai",
        "-e", "POSTGRES_USER=nexusai",
        "-e", f"POSTGRES_PASSWORD={password}",
        "-P", "postgres:16-alpine",
    ])
    endpoint = run([docker, "port", container, "5432/tcp"]).stdout.strip().splitlines()
    if not endpoint or ":" not in endpoint[0]:
        raise RuntimeError(f"Docker did not publish PostgreSQL port: {endpoint!r}")
    port = endpoint[0].rsplit(":", 1)[1]
    environment = os.environ.copy()
    environment.update({
        "NEXUSAI_POSTGRES_HOST": "127.0.0.1",
        "NEXUSAI_POSTGRES_PORT": port,
        "NEXUSAI_POSTGRES_DATABASE": "nexusai",
        "NEXUSAI_POSTGRES_USER": "nexusai",
        "NEXUSAI_POSTGRES_PASSWORD": password,
    })
    return environment, port


def wait_for_postgres(docker: str, container: str) -> None:
    for _ in range(60):
        probe = run([docker, "exec", container, "pg_isready", "-U", "nexusai"], check=False)
        if probe.returncode == 0:
            return
        time.sleep(1)
    raise RuntimeError("PostgreSQL did not become ready")


def invoke(migrator: pathlib.Path, migrations: pathlib.Path,
           environment: dict[str, str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    return run(
        [str(migrator), "--migrations", str(migrations)],
        env=environment,
        check=check,
        timeout=90,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--migrator", required=True)
    parser.add_argument("--migrations", required=True)
    parser.add_argument("--docker", default="docker")
    args = parser.parse_args()

    migrator = pathlib.Path(args.migrator).resolve()
    migrations = pathlib.Path(args.migrations).resolve()
    if not migrator.is_file() or not migrations.is_dir():
        return skip("db_migrate binary or migration directory is unavailable")

    configured_dsn = all(os.environ.get(name) for name in POSTGRES_VARIABLES)
    docker = shutil.which(args.docker)
    container = f"nexusai-migration-test-{uuid.uuid4().hex[:12]}"
    environment: dict[str, str]
    docker_cleanup = False

    if docker:
        try:
            run([docker, "info"], timeout=10)
            environment, _ = docker_dsn(docker, container)
            docker_cleanup = True
            wait_for_postgres(docker, container)
        except (OSError, subprocess.SubprocessError, RuntimeError) as error:
            run([docker, "rm", "-f", container], check=False)
            if not configured_dsn:
                return skip(f"Docker PostgreSQL is unavailable: {error}")
            environment = os.environ.copy()
    elif configured_dsn:
        environment = os.environ.copy()
    else:
        return skip("no Docker PostgreSQL and no complete NEXUSAI_POSTGRES_* test DSN")

    try:
        with tempfile.TemporaryDirectory(prefix="nexusai-migrations-") as temporary:
            empty = pathlib.Path(temporary) / "empty"
            empty.mkdir()
            empty_result = invoke(migrator, empty, environment)
            if "already current" not in empty_result.stdout and "applied" not in empty_result.stdout:
                raise AssertionError(f"empty migration run had unexpected output: {empty_result.stdout}")

            first = invoke(migrator, migrations, environment)
            second = invoke(migrator, migrations, environment)
            if "applied" not in first.stdout or "already current" not in second.stdout:
                raise AssertionError(f"unexpected repeat output:\n{first.stdout}\n{second.stdout}")

            concurrent = [
                subprocess.Popen(
                    [str(migrator), "--migrations", str(migrations)],
                    env=environment,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                )
                for _ in range(2)
            ]
            for process in concurrent:
                output, _ = process.communicate(timeout=90)
                if process.returncode != 0:
                    raise AssertionError(f"concurrent migrator failed ({process.returncode}):\n{output}")

            altered = pathlib.Path(temporary) / "altered"
            shutil.copytree(migrations, altered)
            first_file = sorted(altered.glob("V*__*.sql"))[0]
            first_file.write_bytes(first_file.read_bytes() + b"\n")
            mismatch = invoke(migrator, altered, environment, check=False)
            if mismatch.returncode == 0 or "checksum mismatch" not in mismatch.stdout:
                raise AssertionError(f"checksum mutation was accepted:\n{mismatch.stdout}")

            if docker_cleanup:
                expected = len(list(migrations.glob("V*__*.sql")))
                count = run([
                    docker, "exec", container, "psql", "-U", "nexusai", "-d", "nexusai",
                    "-At", "-c", "SELECT count(*) FROM schema_migrations",
                ]).stdout.strip()
                if int(count) != expected:
                    raise AssertionError(f"expected {expected} migrations, got {count!r}")
        return 0
    finally:
        if docker_cleanup:
            run([docker, "rm", "-f", container], check=False)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:  # pragma: no cover - surfaced by CTest output
        print(f"migration integration test failed: {error}", file=sys.stderr)
        raise SystemExit(1)
