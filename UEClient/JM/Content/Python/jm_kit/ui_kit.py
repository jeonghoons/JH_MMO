"""UI 에셋 도구(UI1, D-270): 폰트 반입.

- 원본: 저장소 SourceArt/Fonts/<글꼴>/ (OFL — 라이선스 파일 함께, 출처는 Docs/ExternalAssets.md)
- 에셋: /Game/UI/Fonts/ — FontFace(글꼴 파일 하나) + Font(F_Body·F_Title, 런타임 캐시 Composite Font)
- Composite Font의 내부 구조체(FTypeface 등)는 파이썬에 노출되지 않으므로, Font 에셋의 CompositeFont 값은
  MCP ObjectTools.set_properties(JSON)로 넣는다 — ui_font_values가 그 JSON을 만든다.

WorldTools.world_step의 "ui_" 단계로 부른다(world.py가 매번 다시 불러옴 — 에디터 재시작 불필요).
"""
import os

import unreal

EAL = unreal.EditorAssetLibrary
FONT_DIR = "/Game/UI/Fonts"
SRC = os.path.normpath(os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()),
                                    "..", "..", "SourceArt", "Fonts"))
# FontFace 에셋 이름 → 원본 파일(SourceArt/Fonts 기준)
FACES = {
    "FF_Pretendard_Regular": "Pretendard/Pretendard-Regular.ttf",
    "FF_Pretendard_Medium": "Pretendard/Pretendard-Medium.ttf",
    "FF_Pretendard_Bold": "Pretendard/Pretendard-Bold.ttf",
    "FF_NotoSerifKR_SemiBold": "NotoSerifKR/NotoSerifKR-SemiBold.otf",
    "FF_NotoSerifKR_Bold": "NotoSerifKR/NotoSerifKR-Bold.otf",
}
# Font 에셋 → 글꼴 이름(TypefaceFontName) → FontFace. 위젯은 FSlateFontInfo.TypefaceFontName으로 굵기를 고른다
FONTS = {
    "F_Body": [("Regular", "FF_Pretendard_Regular"), ("Medium", "FF_Pretendard_Medium"), ("Bold", "FF_Pretendard_Bold")],
    "F_Title": [("Regular", "FF_NotoSerifKR_SemiBold"), ("Bold", "FF_NotoSerifKR_Bold")],
}


def import_fonts(args):
    """원본 글꼴을 FontFace로 반입하고 F_Body·F_Title Font 에셋을 만든다(있으면 그대로). 반환: 만든/있는 경로."""
    tasks = []
    for name, rel in FACES.items():
        if EAL.does_asset_exist(f"{FONT_DIR}/{name}"):
            continue
        src = os.path.join(SRC, rel)
        if not os.path.isfile(src):
            raise RuntimeError(f"원본 없음: {src}")
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", src)
        t.set_editor_property("destination_path", FONT_DIR)
        t.set_editor_property("destination_name", name)
        t.set_editor_property("automated", True)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("save", True)
        tasks.append(t)
    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    faces = {}
    for name in FACES:
        path = f"{FONT_DIR}/{name}"
        a = unreal.load_asset(path)
        faces[name] = {"path": path, "class": a.get_class().get_name() if a else None}
    fonts = {}
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    for name in FONTS:
        path = f"{FONT_DIR}/{name}"
        if not EAL.does_asset_exist(path):
            tools.create_asset(name, FONT_DIR, unreal.Font, unreal.FontFactory())
            EAL.save_asset(path)
        fonts[name] = path
    return {"faces": faces, "fonts": fonts}


def font_values(args):
    """F_Body·F_Title에 넣을 set_properties JSON(문자열)을 돌려준다: {에셋 경로: values}."""
    out = {}
    for name, entries in FONTS.items():
        fonts = [{"Name": tf, "Font": {"FontFaceAsset": f"{FONT_DIR}/{face}.{face}", "Hinting": "Default",
                                        "LoadingPolicy": "LazyLoad"}} for tf, face in entries]
        out[f"{FONT_DIR}/{name}.{name}"] = {"FontCacheType": "Runtime",
                                            "CompositeFont": {"DefaultTypeface": {"Fonts": fonts}}}
    return out


FRAME_DIR = "/Game/UI/Frame"
FRAME_SRC = os.path.normpath(os.path.join(SRC, "..", "UI", "out"))   # SourceArt/UI/draw_ui_frame.py 결과
FRAMES = ("T_UI_Window", "T_UI_TitleBar", "T_UI_Button", "T_UI_Button_Hover", "T_UI_Slot")


def import_frame(args):
    """창 틀·버튼·칸 PNG를 UI 텍스처로 반입한다(UI3, 있으면 다시 반입). 밉맵 없음·UI 압축·UI 텍스처 그룹."""
    tasks = []
    for name in FRAMES:
        src = os.path.join(FRAME_SRC, f"{name}.png")
        if not os.path.isfile(src):
            raise RuntimeError(f"원본 없음: {src} — python -I SourceArt/UI/draw_ui_frame.py 먼저")
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", src)
        t.set_editor_property("destination_path", FRAME_DIR)
        t.set_editor_property("destination_name", name)
        t.set_editor_property("automated", True)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("save", False)
        tasks.append(t)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    out = {}
    for name in FRAMES:
        path = f"{FRAME_DIR}/{name}"
        tex = unreal.load_asset(path)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property("never_stream", True)
        EAL.save_asset(path)
        out[name] = [tex.blueprint_get_size_x(), tex.blueprint_get_size_y()]
    return out


UI_DIR = "/Game/_Core/UI"


def ui_tree(args):
    """WBP의 위젯 트리(디자이너 템플릿)에서 패널·테두리·버튼·이미지 위젯을 나열한다(UI3 조사용).
    args: {"names": ["WBP_Inventory", ...]} 없으면 UI_DIR의 WBP 전부. 반환: {WBP: [[깊이, 클래스, 이름, 브러시/색 요약], ...]}"""
    names = args.get("names") or [a.split("/")[-1].split(".")[0] for a in EAL.list_assets(UI_DIR, recursive=False)
                                  if a.split("/")[-1].startswith("WBP_")]
    kinds = tuple(args.get("kinds") or ("Border", "Button", "Image", "CanvasPanel", "Overlay", "SizeBox", "VerticalBox",
                                        "HorizontalBox", "UniformGridPanel", "ScrollBox", "ProgressBar", "TextBlock"))
    out = {}
    for n in names:
        bp = unreal.load_asset(f"{UI_DIR}/{n}")
        if bp is None:
            continue
        gen = unreal.load_object(None, f"{UI_DIR}/{n}.{n}_C")
        cdo = unreal.get_default_object(gen) if gen else None
        rows = []
        prefix = f"{UI_DIR}/{n}.{n}:WidgetTree."
        for cls_name in kinds:
            cls = getattr(unreal, cls_name)
            for w in unreal.ObjectIterator(cls):
                p = w.get_path_name()
                if not p.startswith(prefix):
                    continue
                info = ""
                try:
                    if cls_name == "Border":
                        b = w.get_editor_property("background")
                        c = w.get_editor_property("brush_color")
                        res = b.get_editor_property("resource_object")
                        info = f"brush={res.get_name() if res else None} draw={b.get_editor_property('draw_as')} color=({c.r:.2f},{c.g:.2f},{c.b:.2f},{c.a:.2f})"
                    elif cls_name == "Image":
                        b = w.get_editor_property("brush")
                        res = b.get_editor_property("resource_object")
                        info = f"brush={res.get_name() if res else None}"
                    elif cls_name == "TextBlock":
                        sc = w.get_editor_property("color_and_opacity").get_editor_property("specified_color")
                        info = f"color=({sc.r:.2f},{sc.g:.2f},{sc.b:.2f},{sc.a:.2f})"
                    elif cls_name == "Button":
                        st = w.get_editor_property("widget_style")
                        nb = st.get_editor_property("normal")
                        info = f"normal_draw={nb.get_editor_property('draw_as')} tint={w.get_editor_property('background_color')}"
                except Exception as e:  # 속성 이름이 버전마다 다를 수 있음
                    info = f"? {e}"
                depth = p[len(prefix):].count(".")
                parent = w.get_parent()
                rows.append([cls_name, w.get_name(), parent.get_name() if parent else "(root)", info])
        out[n] = rows
    return out


def ui_layout(args):
    """WBP의 캔버스 패널 자식 배치(앵커·위치·크기·정렬·크기 자동)를 덤프한다(UI4 조사용). args: {"names": [...]}"""
    out = {}
    for n in args.get("names", []):
        unreal.load_asset(f"{UI_DIR}/{n}")
        prefix = f"{UI_DIR}/{n}.{n}:WidgetTree."
        rows = []
        for s in unreal.ObjectIterator(unreal.CanvasPanelSlot):
            if not s.get_path_name().startswith(prefix):
                continue
            w = s.content if hasattr(s, "content") else None
            try:
                w = s.get_content()
            except Exception:
                pass
            lay = s.get_editor_property("layout_data")
            a = lay.get_editor_property("anchors")
            o = lay.get_editor_property("offsets")
            al = lay.get_editor_property("alignment")
            amin, amax = a.get_editor_property("minimum"), a.get_editor_property("maximum")
            rows.append({"widget": w.get_name() if w else None, "class": w.get_class().get_name() if w else None,
                         "anchor": [round(amin.x, 2), round(amin.y, 2), round(amax.x, 2), round(amax.y, 2)],
                         "offset": [round(o.left), round(o.top), round(o.right), round(o.bottom)],
                         "align": [round(al.x, 2), round(al.y, 2)], "auto": s.get_editor_property("auto_size"),
                         "z": s.get_editor_property("z_order")})
        out[n] = rows
    return out


def ui_set_layout(args):
    """WBP 캔버스 자식의 배치를 바꾸고 컴파일·저장한다(UI4). args: {"wbp": 이름, "items": [{"widget", "anchor": [minx,miny,maxx,maxy],
    "offset": [l,t,r,b], "align": [x,y], "auto": bool}]} — 준 값만 바꾼다. 반환: 바뀐 뒤 배치(ui_layout)."""
    n = args["wbp"]
    bp = unreal.load_asset(f"{UI_DIR}/{n}")
    prefix = f"{UI_DIR}/{n}.{n}:WidgetTree."
    slots = {}
    for s in unreal.ObjectIterator(unreal.CanvasPanelSlot):
        if s.get_path_name().startswith(prefix):
            w = s.get_content()
            if w:
                slots[w.get_name()] = s
    done = []
    for it in args["items"]:
        s = slots.get(it["widget"])
        if s is None:
            raise RuntimeError(f"{n}: 캔버스 자식 {it['widget']} 없음 — 있는 것 {sorted(slots)}")
        if "anchor" in it:
            a = it["anchor"]
            s.set_anchors(unreal.Anchors(minimum=unreal.Vector2D(a[0], a[1]), maximum=unreal.Vector2D(a[2], a[3])))
        if "offset" in it:
            o = it["offset"]
            s.set_offsets(unreal.Margin(o[0], o[1], o[2], o[3]))
        if "align" in it:
            s.set_alignment(unreal.Vector2D(*it["align"]))
        if "auto" in it:
            s.set_auto_size(bool(it["auto"]))
        done.append(it["widget"])
    bp.modify()   # 슬롯 배치만 바꾸면 패키지가 더러워지지 않아 저장이 건너뛰어짐(2026-11-22 확인)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    EAL.save_asset(f"{UI_DIR}/{n}", only_if_is_dirty=False)
    return {"changed": done, "layout": ui_layout({"names": [n]})[n]}


def run(step, args):
    if step == "ui_set_layout":
        return ui_set_layout(args)
    if step == "ui_layout":
        return ui_layout(args)
    if step == "ui_import_frame":
        return import_frame(args)
    if step == "ui_tree":
        return ui_tree(args)
    if step == "ui_import_fonts":
        return import_fonts(args)
    if step == "ui_font_values":
        return font_values(args)
    raise ValueError(f"알 수 없는 ui 단계: {step}")
