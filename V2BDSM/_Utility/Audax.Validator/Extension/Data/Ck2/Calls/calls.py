

class Exports:
	pass

Exports.AllScopes = ['Char', 'Prov', 'Title', 'Global', 'Unit', 'Society', 'Artifact', 'Battle', 'War', 'Siege', 'OffmapPower', 'Religion', 'Culture', 'Bloodline', 'WonderBuilding', 'WonderUpgrade']

def make_any_and_random_scopes(name, source, target):
	return """
(ContextValidator {source}Trigger) = {{ Members = {{
	(N N) = {{ Left = any_{name} Right = {target}TriggerWithCount }}
}} }}
(ContextValidator {source}Command) = {{ Members = {{
	(N N) = {{ Left = any_{name} Right = {target}MaybeLimitCommand }}
	(N N) = {{ Left = random_{name} Right = {target}RandomCommand }}
}} }}""".format(
		name=name,
		source=source,
		target=target,
	)

def make_trigger_and_command_scopes(name, source, target):
	return """
(ContextValidator {source}Trigger) = {{ Members = {{
	(N N) = {{ Left = {name} Right = {target}Trigger }}
}} }}
(ContextValidator {source}Command) = {{ Members = {{
	(N N) = {{ Left = {name} Right = {target}Command }}
}} }}""".format(
		name=name,
		source=source,
		target=target,
	)

Exports.make_any_and_random_scopes = make_any_and_random_scopes
Exports.make_trigger_and_command_scopes = make_trigger_and_command_scopes

EXPORT = {
	'KEY': 'calls',
	'VALUE': Exports
}
