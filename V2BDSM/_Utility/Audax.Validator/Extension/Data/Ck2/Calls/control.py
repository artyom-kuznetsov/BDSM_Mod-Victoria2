
from . import calls

class Exports:
	pass
	
all_scopes = calls.Exports.AllScopes
from_scope_pairs = [
	('Char', 'Bloodline'),
	('Char', 'Char'),
	('Char', 'OffmapPower'),
	('Char', 'Prov'),
	('Char', 'Religion'),
	('Char', 'Society'),
	('Char', 'Title'),
	('Culture', 'Prov'),
	('OffmapPower', 'Char'),
	('Prov', 'Char'),
	('Prov', 'Prov'),
	('Prov', 'Religion'),
    ('Title', 'Char'),
	('Religion', 'Char'),
	('Religion', 'Prov'),
	('Unit', 'Char'),
]
from_from_scope_pairs = [
	('Char', 'Artifact', 'Char'),
	('Char', 'Char', 'Artifact'),
	('Char', 'Char', 'Char'),
	('Char', 'Char', 'Culture'),
	('Char', 'Char', 'OffmapPower'),
	('Char', 'Char', 'Prov'),
	('Char', 'Char', 'Religion'),
	('Char', 'Char', 'Title'),
	('Char', 'Title', 'Char'),
	('Prov', 'Prov', 'Prov'),
	('Char', 'WonderBuilding', 'Prov'),
	('OffmapPower', 'Char', 'Char'),
]
from_from_from_scope_pairs = [
	('Char', 'Char', 'Char', 'Title'),
	('Char', 'Char', 'Title', 'Char'),
	('Char', 'WonderUpgrade', 'WonderBuilding', 'Prov'),
	('Char', 'WonderBuilding', 'Char', 'Prov'),
]
from_from_from_from_scope_pairs = [
	('Char', 'Char', 'Char', 'Char', 'Title'),
]

Exports.FromScopePairs = []


def make_scope(root_scope, from_scope=None, from_from_scope=None, from_from_from_scope=None, from_from_from_from_scope=None, this_scope=None):
	class Obj: pass
	Obj.root_scope = root_scope
	Obj.this_scope = this_scope
	Obj.from_scope = from_scope
	Obj.from_from_scope = from_from_scope
	Obj.from_from_from_scope = from_from_from_scope
	Obj.from_from_from_from_scope = from_from_from_from_scope
	
	suffix = ''
	if this_scope:
		suffix += 'This' + this_scope
	if from_scope:
		suffix += 'From' + from_scope
	if from_from_scope:
		suffix += 'FromFrom' + from_from_scope
	if from_from_from_scope:
		suffix += 'FromFromFrom' + from_from_from_scope
	if from_from_from_from_scope:
		suffix += 'FromFromFromFrom' + from_from_from_from_scope
	Obj.suffix = suffix
	
	Exports.FromScopePairs.append(Obj)

for scope in calls.Exports.AllScopes:
	make_scope(scope)
for scope, from_scope in from_scope_pairs:
	make_scope(scope, from_scope)
for scope, from_scope, from_from_scope in from_from_scope_pairs:
	make_scope(scope, from_scope, from_from_scope)
for scope, from_scope, from_from_scope, from_from_from_scope in from_from_from_scope_pairs:
	make_scope(scope, from_scope, from_from_scope, from_from_from_scope)
for scope, from_scope, from_from_scope, from_from_from_scope, from_from_from_from_scope in from_from_from_from_scope_pairs:
	make_scope(scope, from_scope, from_from_scope, from_from_from_scope, from_from_from_from_scope)

make_scope(root_scope='Title', this_scope='Char', from_scope='Char')

EXPORT = {
	'KEY': 'calls_control',
	'VALUE': Exports
}
