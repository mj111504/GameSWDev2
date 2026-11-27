# -*- coding: utf-8 -*-
"""
언리얼 에디터가 켜질 때 자동으로 실행되는 파일 (Content/Python/init_unreal.py)

1) Scripts/tag_setup_level.py를 t로 불러옴 → Python 칸에서 바로 t.run(), t.ensure() 등 사용 가능
2) AUTO_PLACE_USERS에 있는 PC에서만: House_in_the_tropics를 열 때 반출구역·물건 배치 매니저·슬롯 중
   빠진 것만 자동으로 놓음. 다 있으면 아무것도 안 함. 다른 팀원 PC에서는 맵을 건드리지 않음
"""
import os
import sys
import unreal

AUTO_PLACE_LEVELS = ['House_in_the_tropics']
AUTO_PLACE_USERS = ['yhtgr']   # 자동 배치를 할 Windows 사용자 이름. 팀원도 원하면 여기에 추가

try:
    _project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    _scripts = os.path.join(_project, 'Scripts')
    if _scripts not in sys.path:
        sys.path.append(_scripts)
    import tag_setup_level as t
    import __main__
    __main__.t = t   # Python 칸에서 t로 바로 쓰게
    unreal.log('[TagSetup] Loaded tag_setup_level as t')
except Exception as e:
    t = None
    unreal.log_warning('[TagSetup] Could not load tag_setup_level: %s' % e)

_state = {'world': None}


def _on_tick(_dt):
    # 열린 맵이 바뀌었을 때 한 번만 확인 (Play 중에는 건너뜀)
    try:
        ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        if ues.get_game_world() is not None:
            return
        world = ues.get_editor_world()
        name = world.get_name() if world else None
        if name == _state['world']:
            return
        _state['world'] = name
        if t and name in AUTO_PLACE_LEVELS and os.environ.get('USERNAME', '').lower() in AUTO_PLACE_USERS:
            t.ensure()
    except Exception as e:
        unreal.log_warning('[TagSetup] auto place failed: %s' % e)


if t is not None:
    unreal.register_slate_post_tick_callback(_on_tick)
