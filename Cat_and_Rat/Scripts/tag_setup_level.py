# -*- coding: utf-8 -*-
"""
술래잡기 AI용 맵 배치 스크립트 (언리얼 에디터 Python 콘솔에서 실행)

맵 파일(.umap)은 git으로 합쳐지지 않아서, 팀원이 맵을 푸시하면 우리가 놓은 반출구역·테스트 물건이 사라질 수 있음.
그래서 배치를 이 스크립트에 적어 두고, 맵을 새로 받으면 다시 실행해서 똑같이 놓음.

사용법 (에디터 아래 Cmd 옆을 Python으로 바꾸고 한 줄씩 입력)
    import sys; sys.path.append(r'C:/Users/yhtgr/Documents/GitHub/GameSWDev2/Cat_and_Rat/Scripts'); import tag_setup_level as t
    t.run()                       반출구역 + 테스트 물건을 아래 목록대로 놓음 (이미 있으면 위치·크기만 맞춤)
    t.run(items=False)            반출구역만
    t.add_zone_here('ExportZone_Garage')   뷰포트 카메라 바로 아래 바닥에 반출구역 추가. ZONES에 붙여 넣을 줄을 출력함
    t.list_zones()                지금 맵에 있는 반출구역 목록
    t.remove_test_items()         테스트 물건 지우기
스크립트를 고친 뒤에는  import importlib; importlib.reload(t)

모든 변경은 Ctrl+Z로 되돌릴 수 있음. 저장은 직접 (Ctrl+S).
"""
import unreal

# ---------------- 배치 목록 ----------------
# 반출구역: 이름, 중심 위치(x, y, z), 상자 반지름(x, y, z) cm. 상자 아랫면이 바닥에 닿게 z = 바닥 높이 + 반지름 z
ZONES = [
    ('ExportZone_Roof',     (920.0, -620.0, 1278.0), (300.0, 350.0, 150.0)),   # 옥상 데크, 나선 계단 반대쪽 (바닥 1128)
    ('ExportZone_Basement', (995.0, -298.0, 258.0),  (300.0, 300.0, 150.0)),   # 지하 방 (바닥 108)
]
ZONE_TAG = 'ExportZone'   # C++ TagAgentSubsystem이 이 태그로 반출구역을 찾음

# 테스트 물건: 이름, 위치, 무게(1/3/6/10). 팀원 물건 자동 배치가 들어오면 run(items=False)만 쓰면 됨
ITEM_BP = '/Game/ltk/MyPickupItem'
ITEM_FOLDER = 'TagAITest'
ITEMS = [
    ('TestItem_w1_1', (400.0, -1700.0, 460.0), 1),
    ('TestItem_w1_2', (0.0, -300.0, 880.0), 1),
    ('TestItem_w3_3', (1400.0, -1400.0, 460.0), 3),
    ('TestItem_w3_4', (-100.0, -900.0, 464.0), 3),
    ('TestItem_w6_5', (-100.0, 200.0, 460.0), 6),
    ('TestItem_w6_6', (800.0, -1500.0, 880.0), 6),
]
SIZE_NAME = {1: 'Small', 3: 'Medium', 6: 'Large', 10: 'ExtraLarge'}


# ---------------- 내부 함수 ----------------
def _eas():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def _find(label):
    for a in _eas().get_all_level_actors():
        if a.get_actor_label() == label:
            return a
    return None


def _v(t):
    return unreal.Vector(float(t[0]), float(t[1]), float(t[2]))


def _place_zone(name, loc, extent):
    a = _find(name)
    made = a is None
    if made:
        a = _eas().spawn_actor_from_class(unreal.TriggerBox, _v(loc))
        a.set_actor_label(name)
    a.modify()
    a.set_actor_location(_v(loc), False, False)
    box = a.get_component_by_class(unreal.BoxComponent)
    box.modify()
    box.set_box_extent(_v(extent))
    tags = list(a.tags)
    if unreal.Name(ZONE_TAG) not in tags:
        tags.append(unreal.Name(ZONE_TAG))
        a.set_editor_property('tags', tags)
    _report_nav(name, loc, extent)
    return made


def _report_nav(name, loc, extent):
    # 상자 바닥 가운데에 NavMesh가 있는지 확인 (없으면 쥐가 못 들어감)
    floor = unreal.Vector(loc[0], loc[1], loc[2] - extent[2])
    nav = unreal.NavigationSystemV1.project_point_to_navigation(_world(), floor, None, None, unreal.Vector(80, 80, 120))
    if nav is None:
        unreal.log_warning('[TagSetup] %s: no NavMesh near the floor of this zone. Rats may not be able to reach it.' % name)


def _place_item(cls, cube, label, loc, weight):
    a = _find(label)
    made = a is None
    if made:
        a = _eas().spawn_actor_from_class(cls, _v(loc))
        a.set_actor_label(label)
    a.modify()
    a.set_actor_location(_v(loc), False, False)
    info = a.get_editor_property('item_info')
    info.set_editor_property('item_name', 'Test%s_%s' % (SIZE_NAME.get(weight, 'W%d' % weight), label.split('_')[-1]))
    info.set_editor_property('weight', int(weight))
    info.set_editor_property('mass', float(weight * 5))
    a.set_editor_property('item_info', info)
    mesh = a.get_component_by_class(unreal.StaticMeshComponent)
    if mesh and cube:
        mesh.modify()
        mesh.set_static_mesh(cube)
        mesh.set_relative_scale3d(unreal.Vector(0.4, 0.4, 0.4))
        mesh.set_relative_location(unreal.Vector(0, 0, -30), False, False)
    a.set_folder_path(ITEM_FOLDER)
    return made


# ---------------- 쓰는 함수 ----------------
def run(zones=True, items=True):
    """목록대로 반출구역·테스트 물건을 놓음. 이미 있으면 위치·크기만 목록에 맞춤"""
    made_z = fixed_z = made_i = fixed_i = 0
    with unreal.ScopedEditorTransaction('Tag setup level'):
        if zones:
            for name, loc, ext in ZONES:
                if _place_zone(name, loc, ext):
                    made_z += 1
                else:
                    fixed_z += 1
        if items:
            cls = unreal.EditorAssetLibrary.load_blueprint_class(ITEM_BP)
            cube = unreal.EditorAssetLibrary.load_asset('/Engine/BasicShapes/Cube')
            if cls is None:
                unreal.log_error('[TagSetup] Item blueprint not found: %s' % ITEM_BP)
            else:
                for label, loc, w in ITEMS:
                    if _place_item(cls, cube, label, loc, w):
                        made_i += 1
                    else:
                        fixed_i += 1
    unreal.log('[TagSetup] zones: %d new, %d updated / items: %d new, %d updated. Save the level with Ctrl+S.'
               % (made_z, fixed_z, made_i, fixed_i))


def add_zone_here(name, ex=300.0, ey=300.0, ez=150.0):
    """뷰포트 카메라 바로 아래 바닥에 반출구역을 놓음. ZONES 목록에 붙여 넣을 줄을 출력함"""
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    cam = ues.get_level_viewport_camera_info()[0]
    trace = getattr(unreal.TraceTypeQuery, 'ECC_VISIBILITY', None) or unreal.TraceTypeQuery.TRACE_TYPE_QUERY1
    hit = unreal.SystemLibrary.line_trace_single(_world(), cam, cam + unreal.Vector(0, 0, -2000), trace,
                                                 True, [], unreal.DrawDebugTrace.NONE, True)
    if hit is None:
        unreal.log_error('[TagSetup] No floor found below the camera')
        return
    try:
        floor = hit.to_tuple()[4]
    except Exception:
        floor = hit.get_editor_property('location')
    loc = (round(cam.x), round(cam.y), round(floor.z + ez))
    with unreal.ScopedEditorTransaction('Tag add zone'):
        _place_zone(name, loc, (ex, ey, ez))
    unreal.log('[TagSetup] Added %s. Paste this into ZONES:' % name)
    unreal.log("    ('%s', (%.1f, %.1f, %.1f), (%.1f, %.1f, %.1f))," % (name, loc[0], loc[1], loc[2], ex, ey, ez))


def list_zones():
    for a in _eas().get_all_level_actors():
        if unreal.Name(ZONE_TAG) in list(a.tags):
            box = a.get_component_by_class(unreal.BoxComponent)
            ext = box.get_unscaled_box_extent() if box else None
            unreal.log('[TagSetup] %s at %s extent %s' % (a.get_actor_label(), a.get_actor_location(), ext))


def remove_test_items():
    labels = set(l for l, _, _ in ITEMS)
    with unreal.ScopedEditorTransaction('Tag remove test items'):
        for a in _eas().get_all_level_actors():
            if a.get_actor_label() in labels:
                _eas().destroy_actor(a)
    unreal.log('[TagSetup] Removed test items')
