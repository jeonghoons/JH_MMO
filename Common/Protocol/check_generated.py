"""생성된 Protobuf 코드(.pb.h/.pb.cc)가 .proto와 맞는지 검사합니다.

.proto를 임시 폴더에서 protoc로 다시 생성해, 추적 중인 세 사본
(Common/Protocol, Server/Server/Protocol, UEClient/JM/Source/JM/Protocol)과 비교합니다.
줄바꿈(CRLF/LF) 차이는 무시합니다(core.autocrlf=true, .gitattributes text=auto).

사용법:
  python -I Common/Protocol/check_generated.py              작업 폴더 기준 검사(수동 확인)
  python -I Common/Protocol/check_generated.py --staged     스테이징된 내용 기준 검사(git pre-commit 훅)
  python -I Common/Protocol/check_generated.py --stop-hook  Claude Code Stop 훅(stdin JSON, 낡았으면 exit 2)

고치는 방법은 항상 같습니다: Common/Protocol/GanPackets.bat nopause 실행 → (커밋이면) 생성 파일 git add.
"""

import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROTO_DIR = ROOT / "Common" / "Protocol"
PROTOC = PROTO_DIR / "protoc.exe"
PROTOS = ["Enum.proto", "Struct.proto", "Protocol.proto"]
OUT_DIRS = ["Common/Protocol", "Server/Server/Protocol", "UEClient/JM/Source/JM/Protocol"]
GENERATED = [p.replace(".proto", ext) for p in PROTOS for ext in (".pb.h", ".pb.cc")]

PROTO_PATHS = [f"Common/Protocol/{p}" for p in PROTOS]
GEN_PATHS = [f"{d}/{g}" for d in OUT_DIRS for g in GENERATED]
WATCHED = PROTO_PATHS + GEN_PATHS

FIX_HINT = (
    "고치는 방법: Common/Protocol/GanPackets.bat 을 인자 nopause로 실행해 다시 생성하세요"
    " (생성 파일은 손으로 고치지 않습니다)."
)


def git(*args: str) -> bytes:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, check=True).stdout


def read_source(rel: str, staged: bool) -> bytes | None:
    if staged:
        try:
            return git("show", f":{rel}")
        except subprocess.CalledProcessError:
            return None
    path = ROOT / rel
    return path.read_bytes() if path.exists() else None


def norm(data: bytes) -> bytes:
    return data.replace(b"\r\n", b"\n")


def generate(staged: bool) -> dict[str, bytes]:
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "src"
        out = Path(tmp) / "out"
        src.mkdir()
        out.mkdir()
        for proto in PROTOS:
            data = read_source(f"Common/Protocol/{proto}", staged)
            if data is None:
                raise RuntimeError(f"{proto}을(를) 찾을 수 없습니다.")
            (src / proto).write_bytes(data)
        result = subprocess.run(
            [str(PROTOC), f"--proto_path={src}", f"--cpp_out={out}", *PROTOS],
            cwd=src,
            capture_output=True,
        )
        if result.returncode != 0:
            message = result.stderr.decode("utf-8", "replace").strip()
            raise RuntimeError(f"protoc 실패(.proto 문법 오류일 수 있음):\n{message}")
        return {name: (out / name).read_bytes() for name in GENERATED}


def find_problems(staged: bool) -> list[str]:
    try:
        expected = generate(staged)
    except RuntimeError as error:
        return [str(error)]
    problems = []
    for rel in GEN_PATHS:
        actual = read_source(rel, staged)
        name = rel.rsplit("/", 1)[1]
        if actual is None:
            problems.append(f"없음: {rel}")
        elif norm(actual) != norm(expected[name]):
            problems.append(f"낡음: {rel}")
    return problems


def changed_paths(staged: bool) -> list[str]:
    if staged:
        out = git("diff", "--cached", "--name-only", "--", *WATCHED)
        return out.decode("utf-8").split()
    out = git("status", "--porcelain", "--", *WATCHED)
    return [line[3:] for line in out.decode("utf-8").splitlines() if line.strip()]


def report(problems: list[str], where: str) -> str:
    lines = [f"[프로토콜 검사] {where}의 생성 코드가 .proto와 맞지 않습니다."]
    lines += [f"  - {p}" for p in problems]
    lines.append(FIX_HINT)
    return "\n".join(lines)


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    mode = sys.argv[1] if len(sys.argv) > 1 else ""

    if mode == "--stop-hook":
        try:
            payload = json.loads(sys.stdin.read() or "{}")
        except json.JSONDecodeError:
            payload = {}
        if payload.get("stop_hook_active"):
            return 0  # 이미 한 번 막았음: 무한 반복 방지
        if not changed_paths(staged=False):
            return 0  # 프로토콜 관련 파일이 HEAD와 같음(HEAD는 pre-commit이 지킴)
        problems = find_problems(staged=False)
        if problems:
            print(report(problems, "작업 폴더"), file=sys.stderr)
            return 2  # Claude가 응답을 끝내지 못하고 이 메시지를 받음
        return 0

    if mode == "--staged":
        if not changed_paths(staged=True):
            return 0
        problems = find_problems(staged=True)
        if problems:
            print(report(problems, "스테이징된 내용"), file=sys.stderr)
            print("재생성 뒤에는 세 폴더의 .pb.* 파일을 git add 하세요.", file=sys.stderr)
            return 1
        return 0

    problems = find_problems(staged=False)
    if problems:
        print(report(problems, "작업 폴더"))
        return 1
    print("[프로토콜 검사] 세 곳의 생성 코드가 모두 .proto와 맞습니다.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
