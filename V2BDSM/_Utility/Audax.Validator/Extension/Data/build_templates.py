import pathlib
import jinja2
from jinja2 import Template
import sys
import importlib

do_output = False
if any(arg == '--do-output' for arg in sys.argv):
	do_output = True
	
for dir in pathlib.Path('.').glob('*/'):
	py_files = list(pathlib.Path(dir).glob('**/*.py'))
	
	class exports: pass
	
	for py_file in py_files:
		mod = importlib.import_module(str(py_file).replace('.py', '').replace('\\', '.'))
		if hasattr(mod, 'EXPORT'):
			export_dict = getattr(mod, 'EXPORT')
			key = export_dict['KEY']
			if hasattr(exports, key):
				raise Exception("Duplicate key " + key)
			setattr(exports, key, export_dict['VALUE'])
	
	files = list(pathlib.Path(dir).glob('**/*.template'))

	for file in files:
		with file.open() as f:
			in_text = f.read()

		template = Template(source=in_text, undefined=jinja2.StrictUndefined)
		out_text = template.render(ctx = exports)
		
		parts = list(file.parts)
		parts[-1] = parts[-1].replace('.template', '')
		out_path = pathlib.Path('/'.join(parts))
		if do_output:
			with out_path.open('w') as f:
				f.write(out_text)
