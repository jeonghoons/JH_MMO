"""Epic 공식 MCP에 등록하는 프로젝트 툴셋(`toolset.KitTools`).

공식 `execute_tool_script`는 json·math 등 일부 표준 모듈만 import할 수 있어
`jm_kit`을 부를 수 없다. 그래서 jm_kit의 정해진 함수만 MCP 도구로 감싸 등록한다.
서버에 인증이 없으므로 임의 코드 실행 도구는 두지 않는다.
등록은 `Content/Python/init_unreal.py`에서 한다.
"""
