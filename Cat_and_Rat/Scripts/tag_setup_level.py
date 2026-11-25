# -*- coding: utf-8 -*-
"""
술래잡기 AI용 맵 배치 스크립트 (언리얼 에디터 Python 콘솔에서 실행)

맵 파일(.umap)은 git으로 합쳐지지 않아서, 팀원이 맵을 푸시하면 우리가 놓은 반출구역·테스트 물건이 사라질 수 있음.
그래서 배치를 이 스크립트에 적어 두고, 맵을 새로 받으면 다시 실행해서 똑같이 놓음.

자동 실행: Content/Python/init_unreal.py가 에디터를 켤 때 이 파일을 t로 불러옴.
          AUTO_PLACE_USERS에 있는 PC에서는 House_in_the_tropics를 열 때 빠진 것만 자동으로 놓음 (ensure)
          그래서 보통은 아무것도 안 쳐도 됨. 직접 쓸 때는 Python 칸에서 t.명령()

사용법 (에디터 아래 Cmd 옆을 Python으로 바꾸고 입력. t는 이미 불러와져 있음)
    t.ensure()                    빠진 것만 놓음
    t.run()                       반출구역 + 물건 배치 매니저 + 슬롯을 아래 목록대로 놓음 (이미 있으면 위치·크기만 맞춤)
    t.run(spawn=False)            반출구역만
    t.run(items=True)             예전 테스트 물건 6개도 같이 (매니저를 쓰면 필요 없음)

  슬롯 정하기
    t.gen_slots(24)               NavMesh 위에서 서로 최대한 떨어진 24곳을 골라 슬롯을 놓음 (반출구역 근처 제외)
                                  → 위에서 보며 이상한 슬롯을 옮기거나 지움
    t.capture_slots()             지금 맵의 슬롯 위치를 SLOTS 목록 형태로 출력 → 아래 SLOTS에 붙여 넣고 저장
    t.remove_spawn()              매니저와 슬롯 지우기
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

# 물건 배치: 팀원의 배치 매니저 BP와 슬롯 박스(C++). 게임 시작 때 매니저가 슬롯마다 물건을 하나씩 생성함
SPAWN_MANAGER_BP = '/Game/ltk/MyItemSpawnManager'
SPAWN_MANAGER_LABEL = 'TagSpawnManager'
SPAWN_MANAGER_LOC = (0.0, 0.0, 600.0)
SLOT_CLASS = '/Script/Cat_and_Rat.SpawnSlotBox'
SLOT_PREFIX = 'TagSlot_'
SPAWN_FOLDER = 'TagSpawn'
# 슬롯 위치 (x, y, z). z = 바닥 + 50 (물건 충돌 구 반지름). t.capture_slots() 결과를 붙여 넣음
SLOTS = [   # 24 slots, 2026-11-25 손으로 맞춤 (지하 ~150, 1층 ~460, 2층 ~880, 옥상 ~1180)
    (326, 747, 313),
    (1530, -2470, 880),
    (-57, 446, 490),
    (-670, -1700, 460),
    (1500, -1400, 167),
    (1520, -200, 880),
    (408, -1069, 1180),
    (530, -2900, 460),
    (-478, -228, 450),
    (480, -940, 174),
    (1630, 100, 160),
    (-644, -2166, 450),
    (1397, -139, 1180),
    (-480, 850, 469),
    (630, -1600, 460),
    (1520, -1200, 1180),
    (1630, 720, 150),
    (930, -400, 460),
    (399, 297, 300),
    (1234, -2620, 450),
    (-481, -2725, 490),
    (1241, 886, 190),
    (330, -700, 880),
    (1032, -1438, 880),
]


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


def _place_spawn():
    """매니저 1개 + SLOTS 목록대로 슬롯을 놓음. 목록에 없는 TagSlot_ 슬롯은 지움"""
    mgr_cls = unreal.EditorAssetLibrary.load_blueprint_class(SPAWN_MANAGER_BP)
    slot_cls = unreal.load_class(None, SLOT_CLASS)
    if mgr_cls is None or slot_cls is None:
        unreal.log_error('[TagSetup] Spawn manager BP or SpawnSlotBox class not found')
        return 0
    m = _find(SPAWN_MANAGER_LABEL)
    if m is None:
        m = _eas().spawn_actor_from_class(mgr_cls, _v(SPAWN_MANAGER_LOC))
        m.set_actor_label(SPAWN_MANAGER_LABEL)
    m.set_folder_path(SPAWN_FOLDER)

    for i, loc in enumerate(SLOTS):
        label = '%s%02d' % (SLOT_PREFIX, i + 1)
        a = _find(label)
        if a is None:
            a = _eas().spawn_actor_from_class(slot_cls, _v(loc))
            a.set_actor_label(label)
        a.modify()
        a.set_actor_location(_v(loc), False, False)
        a.set_folder_path(SPAWN_FOLDER)
    wanted = set('%s%02d' % (SLOT_PREFIX, i + 1) for i in range(len(SLOTS)))
    for a in _eas().get_all_level_actors():
        lab = a.get_actor_label()
        if lab.startswith(SLOT_PREFIX) and lab not in wanted:
            _eas().destroy_actor(a)
    return len(SLOTS)


# ---------------- 쓰는 함수 ----------------
def run(zones=True, spawn=True, items=False):
    """목록대로 반출구역·배치 매니저·슬롯(·테스트 물건)을 놓음. 이미 있으면 위치·크기만 목록에 맞춤"""
    made_z = fixed_z = made_i = fixed_i = 0
    n_slots = 0
    with unreal.ScopedEditorTransaction('Tag setup level'):
        if spawn:
            if not SLOTS:
                unreal.log_warning('[TagSetup] SLOTS is empty, so no spawn manager/slots placed. Run t.gen_slots() first, then t.capture_slots()')
            else:
                n_slots = _place_spawn()
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
    unreal.log('[TagSetup] zones: %d new, %d updated / spawn manager + %d slots / test items: %d new, %d updated. Save the level with Ctrl+S.'
               % (made_z, fixed_z, n_slots, made_i, fixed_i))


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


def ensure():
    """반출구역·매니저·슬롯 중 빠진 게 있을 때만 run(). 다 있으면 아무것도 안 함 (맵이 '저장 안 됨'이 되지 않게)"""
    labels = set(a.get_actor_label() for a in _eas().get_all_level_actors())
    need = [n for n, _, _ in ZONES if n not in labels]
    if SLOTS:
        need += [SPAWN_MANAGER_LABEL] if SPAWN_MANAGER_LABEL not in labels else []
        need += ['%s%02d' % (SLOT_PREFIX, i + 1) for i in range(len(SLOTS)) if '%s%02d' % (SLOT_PREFIX, i + 1) not in labels]
    if need:
        unreal.log('[TagSetup] Missing %d actors (%s ...). Placing them now.' % (len(need), ', '.join(need[:3])))
        run()
    else:
        unreal.log('[TagSetup] Export zones, spawn manager and slots are already in this level.')


# ---------------- 슬롯 자동 배치 ----------------
def gen_slots(n=24, min_dist=300.0, zone_clear=400.0, seed=7):
    """NavMesh 위 후보 지점 중 서로 최대한 떨어진 n곳에 슬롯을 놓음 (가장 먼 점 고르기)
    - 후보: NavMeshBoundsVolume 범위를 1m 간격으로 훑어 NavMesh 위로 끌어온 점 (층마다 따로)
    - 반출구역 중심에서 zone_clear(cm) 안은 제외
    - 이미 고른 점들과의 최소 거리가 가장 큰 후보를 하나씩 추가 (층이 다르면 높이 차이를 3배로 쳐서 층마다 퍼지게)
    - 최소 거리가 min_dist보다 작아지면 멈춤
    """
    import random
    world = _world()
    nav = unreal.NavigationSystemV1
    vols = [a for a in _eas().get_all_level_actors() if isinstance(a, unreal.NavMeshBoundsVolume)]
    if not vols:
        unreal.log_error('[TagSetup] No NavMeshBoundsVolume in this level')
        return
    zones = [_v(loc) for _, loc, _ in ZONES]

    cand = {}
    for v in vols:
        origin, ext = v.get_actor_bounds(False)
        x0, x1 = int(origin.x - ext.x), int(origin.x + ext.x)
        y0, y1 = int(origin.y - ext.y), int(origin.y + ext.y)
        z0, z1 = int(origin.z - ext.z), int(origin.z + ext.z)
        for x in range(x0, x1, 100):
            for y in range(y0, y1, 100):
                for z in range(z0, z1, 100):
                    p = nav.project_point_to_navigation(world, unreal.Vector(x, y, z), None, None, unreal.Vector(60, 60, 55))
                    if p is None:
                        continue
                    key = (int(p.x // 50), int(p.y // 50), int(p.z // 60))
                    cand[key] = p
    pts = [p for p in cand.values()
           if all(((p.x - zc.x) ** 2 + (p.y - zc.y) ** 2) ** 0.5 > zone_clear or abs(p.z - (zc.z - 150)) > 200 for zc in zones)]
    if not pts:
        unreal.log_error('[TagSetup] No NavMesh points found')
        return

    def d(a, b):
        return ((a.x - b.x) ** 2 + (a.y - b.y) ** 2 + (3.0 * (a.z - b.z)) ** 2) ** 0.5

    random.seed(seed)
    chosen = [random.choice(pts)]
    best = [d(p, chosen[0]) for p in pts]
    while len(chosen) < n:
        i = max(range(len(pts)), key=lambda k: best[k])
        if best[i] < min_dist:
            break
        chosen.append(pts[i])
        best = [min(best[k], d(pts[k], pts[i])) for k in range(len(pts))]

    global SLOTS
    SLOTS = [(round(p.x), round(p.y), round(p.z + 50)) for p in chosen]
    with unreal.ScopedEditorTransaction('Tag gen slots'):
        _place_spawn()
    unreal.log('[TagSetup] %d candidate points, placed %d slots. Check them from the top view, move/delete bad ones, then t.capture_slots()'
               % (len(pts), len(SLOTS)))


def capture_slots():
    """지금 맵에 있는 TagSlot_ 슬롯 위치를 SLOTS 목록 형태로 출력. 이 줄들을 위 SLOTS = [ ] 안에 붙여 넣음"""
    slots = sorted([a for a in _eas().get_all_level_actors() if a.get_actor_label().startswith(SLOT_PREFIX)],
                   key=lambda a: a.get_actor_label())
    unreal.log('[TagSetup] SLOTS = [   # %d slots' % len(slots))
    for a in slots:
        p = a.get_actor_location()
        unreal.log('    (%d, %d, %d),   # [TagSetup] %s' % (round(p.x), round(p.y), round(p.z), a.get_actor_label()))
    unreal.log('[TagSetup] ]')


def remove_spawn():
    with unreal.ScopedEditorTransaction('Tag remove spawn'):
        for a in _eas().get_all_level_actors():
            lab = a.get_actor_label()
            if lab == SPAWN_MANAGER_LABEL or lab.startswith(SLOT_PREFIX):
                _eas().destroy_actor(a)
    unreal.log('[TagSetup] Removed spawn manager and slots')
