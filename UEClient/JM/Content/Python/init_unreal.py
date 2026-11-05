# 에디터 시작 시 자동 실행 — 프로젝트 툴셋을 Epic 공식 MCP(ToolsetRegistry)에 등록
import unreal

try:
    from jm_mcp import toolset as _jm_toolset
    if not _jm_toolset.registration.register():
        unreal.log_warning("jm_mcp: ToolsetRegistry를 사용할 수 없어 등록하지 못함")
except Exception as e:  # 툴셋 플러그인이 꺼진 환경(게임 빌드 등)에서도 에디터는 계속 뜬다
    unreal.log_warning(f"jm_mcp: 툴셋 등록 실패 — {e}")
