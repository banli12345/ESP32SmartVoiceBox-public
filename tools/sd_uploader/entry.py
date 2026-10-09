"""PyInstaller entry and subprocess conversion dispatcher."""
import json
import sys
from pathlib import Path

def main():
    if len(sys.argv)>1 and sys.argv[1]=='--convert-job':
        from tools.sd_uploader.worker import main as worker
        try:
            return worker(sys.argv[2:])
        except Exception:
            import traceback
            target=Path(sys.argv[4])
            target.with_name(target.name+'.error.txt').write_text(traceback.format_exc(),encoding='utf-8')
            return 1
    if len(sys.argv)>2 and sys.argv[1]=='--self-test':
        from tools.sd_uploader.converters import find_ffmpeg
        from tools.sd_uploader.gui import UploaderApp
        import tkinter as tk
        import subprocess
        root=tk.Tk(); root.withdraw(); app=UploaderApp(root); root.update_idletasks()
        executable=find_ffmpeg()
        result=subprocess.run([executable,'-version'],capture_output=True,
                              creationflags=subprocess.CREATE_NO_WINDOW if sys.platform=='win32' else 0)
        data=dict(gui=True,ffmpeg=result.returncode==0,ports=len(app.port_map),
                  queue_columns=list(app.tree['columns']),ffmpeg_path=executable)
        root.destroy()
        Path(sys.argv[2]).write_text(json.dumps(data,ensure_ascii=False),encoding='utf-8')
        return 0 if data['ffmpeg'] else 1
    from tools.sd_uploader.gui import main as gui
    gui()
    return 0

if __name__=='__main__': raise SystemExit(main())
