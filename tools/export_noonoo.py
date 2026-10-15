"""Export the vacuum's steering setup from the original Unity scene."""
import json
import numpy as np


def export_noonoo(e, scene, objects, types, transforms, matrix):
    root = next(p for p, d in objects.items()
                if types[p] == 'GameObject' and d.get('m_Name') == 'vacum')
    component = next(d for p, d in objects.items()
                     if types[p] == 'MonoBehaviour' and d['m_GameObject']['m_PathID'] == root)
    # The source has one fixed target with its trigger disabled. Forward
    # steering circles that target; there is no two-point ping-pong route.
    assert len(component['waypoints']) == 1 and component['smoothRotation']
    target = component['waypoints'][0]['m_PathID']
    target_go = objects[target]['m_GameObject']['m_PathID']
    triggers = [d for p, d in objects.items() if types[p].endswith('Collider')
                and d['m_GameObject']['m_PathID'] == target_go
                and d.get('m_Enabled') and d.get('m_IsTrigger')]
    assert not triggers, 'Vacuum waypoint trigger needs an explicit port'
    rotation = objects[transforms[root]]['m_LocalRotation']
    quaternion = np.array([rotation[k] for k in 'xyzw'])
    quaternion /= np.linalg.norm(quaternion)
    settings = [component[k] for k in ('accel', 'speedLimit', 'rotationDamping')]
    def values(row):
        return ','.join(f'{float(v):.9f}f' for v in row)
    (e.out / 'noonoo.h').write_text(
        '/* Original vacuum root rotation and steering parameters. */\n'
        'static const float source_hoover_rotation[4]={' + values(quaternion) + '};\n'
        'static const float source_hoover_settings[3]={' + values(settings) + '};\n')
    animation = next(d for p, d in objects.items()
                     if types[p] == 'Animation' and d['m_GameObject']['m_PathID'] == root)
    clip = e.resolve(scene, animation['m_Animation']).read_typetree()
    report = dict(source='Assets/hooverscript.js', root='vacum',
                  root_position=matrix(transforms[root])[:3, 3].tolist(),
                  root_rotation=quaternion.tolist(), target=matrix(target)[:3, 3].tolist(),
                  parameters={k: component[k] for k in
                              ('accel', 'inertia', 'speedLimit', 'minSpeed', 'stopTime', 'rotationDamping')},
                  target_trigger_enabled=False, idle_clip=clip['m_Name'],
                  idle_rotation_paths=[c['path'] for c in clip['m_RotationCurves']],
                  idle_clip_ported=False)
    (e.out / 'noonoo.json').write_text(json.dumps(report, indent=2))
