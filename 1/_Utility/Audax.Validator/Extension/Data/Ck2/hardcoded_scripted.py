

class Exports:
	pass

# list of [{'name': X, 'root': X}]
Exports.Effects = []

seen_names = set()

def add_action(name, root_scope, from_scope=None):
	if name in seen_names:
		raise Exception("Duplicate on_action " + name)
		
	seen_names.add(name)

	class Obj: pass
	Obj.name = name
	Obj.root_scope = root_scope
	Obj.from_scope = from_scope
	effect_scope_target_name = root_scope + 'Command'
	if from_scope:
		effect_scope_target_name = effect_scope_target_name + 'From' + from_scope
	Obj.effect_scope_target_name = effect_scope_target_name
	Exports.Effects.append(Obj)

# root=char
for name in [
	'pledge_crusade_effect',
    'unpledge_crusade_effect',
    'defender_pledge_crusade_effect',
    'defender_unpledge_crusade_effect',
    'contribute_to_crusade_pot_effect',
    'select_crusade_receiver_stance_effect',
    'crusade_give_artifact_effect',
]:
	add_action(name, root_scope='Char')

add_action('rename_character_effect', root_scope='Char', from_scope='Char')

EXPORT = {
	'KEY': 'hardcoded_scripted',
	'VALUE': Exports
}
