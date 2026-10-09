"""Chinese Tkinter desktop UI. All conversion and UART work runs off-thread."""
from __future__ import annotations
import queue
import tempfile
import threading
import time
from pathlib import Path
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from serial.tools import list_ports
from .converters import KINDS, MediaJob, application_dir, plan_jobs, predicted_targets, prepare_jobs, export_files
from .transport import SerialUploader, UploadCancelled

class UploaderApp:
    def __init__(self, root: tk.Tk):
        self.root=root
        self.jobs=[]
        self.events=queue.Queue()
        self.cancel_event=threading.Event()
        self.busy=False
        self.closing=False
        self.committing=False
        self.port_map={}
        self.transfer_start=0
        self.port=tk.StringVar()
        self.kind=tk.StringVar(value=KINDS['wallpaper'])
        self.slot=tk.StringVar(value='1')
        self.fps=tk.StringVar(value='20')
        self.status=tk.StringVar(value='添加素材后，选择串口并点击上传按钮')
        self.detail=tk.StringVar(value='壁纸 480×480  ·  照片 480×450  ·  视频 320×240 + 同名音轨')
        self.connection=tk.StringVar()
        root.title('ESP32 SD 素材上传工具')
        root.geometry('1080x760')
        root.minsize(940,650)
        root.configure(bg='#f3f5f8')
        style=ttk.Style(root)
        try: style.theme_use('vista')
        except tk.TclError: style.theme_use('clam')
        style.configure('TLabel',font=('Microsoft YaHei UI',10))
        style.configure('TButton',font=('Microsoft YaHei UI',10),padding=(12,7))
        style.configure('Treeview',font=('Microsoft YaHei UI',10),rowheight=32)
        style.configure('Treeview.Heading',font=('Microsoft YaHei UI',10,'bold'))
        outer=ttk.Frame(root,padding=22)
        outer.pack(fill='both',expand=True)
        ttk.Label(outer,text='ESP32 · SD 素材上传',font=('Microsoft YaHei UI',22,'bold')).pack(anchor='w')
        ttk.Label(outer,text='图片、音乐和视频自动转换；LRC 歌词原样上传到 SD 卡',foreground='#536078').pack(anchor='w',pady=(4,16))
        connection=ttk.LabelFrame(outer,text='设备',padding=(12,8))
        connection.pack(fill='x')
        ttk.Label(connection,text='串口').pack(side='left')
        self.port_box=ttk.Combobox(connection,textvariable=self.port,state='readonly',width=35)
        self.port_box.pack(side='left',padx=10)
        self.port_box.bind('<<ComboboxSelected>>',lambda _:self._buttons())
        self.refresh_button=ttk.Button(connection,text='刷新串口',command=self.refresh_ports)
        self.refresh_button.pack(side='left')
        ttk.Label(connection,textvariable=self.connection,foreground='#536078').pack(side='right',padx=6)
        controls=ttk.Frame(outer)
        controls.pack(fill='x',pady=(16,10))
        ttk.Label(controls,text='素材类别').pack(side='left')
        self.kind_box=ttk.Combobox(controls,textvariable=self.kind,values=list(KINDS.values()),state='readonly',width=15)
        self.kind_box.pack(side='left',padx=(8,14))
        self.kind_box.bind('<<ComboboxSelected>>',lambda _:self._options())
        self.slot_label=ttk.Label(controls,text='壁纸位置')
        self.slot_label.pack(side='left')
        self.slot_box=ttk.Combobox(controls,textvariable=self.slot,values=['1','2','3'],state='readonly',width=3)
        self.slot_box.pack(side='left',padx=(8,14))
        self.fps_label=ttk.Label(controls,text='视频帧率')
        self.fps_label.pack(side='left')
        self.fps_box=ttk.Combobox(controls,textvariable=self.fps,values=['10','15','20','25','30'],state='disabled',width=4)
        self.fps_box.pack(side='left',padx=(8,14))
        self.add_button=ttk.Button(controls,text='添加文件…',command=self.add_files)
        self.add_button.pack(side='left')
        self.remove_button=ttk.Button(controls,text='移除选中',command=self.remove_selected)
        self.remove_button.pack(side='right',padx=(8,0))
        self.clear_button=ttk.Button(controls,text='清空',command=self.clear)
        self.clear_button.pack(side='right')
        table=ttk.Frame(outer)
        table.pack(fill='both',expand=True)
        columns=('kind','source','target','format')
        self.tree=ttk.Treeview(table,columns=columns,show='headings',selectmode='extended',height=7)
        for col,title,width in [('kind','类别',110),('source','原始文件',230),('target','SD 卡目标',380),('format','处理方式',185)]:
            self.tree.heading(col,text=title)
            self.tree.column(col,width=width,minwidth=80,stretch=col in ('source','target'))
        scroll=ttk.Scrollbar(table,orient='vertical',command=self.tree.yview)
        self.tree.configure(yscrollcommand=scroll.set)
        self.tree.pack(side='left',fill='both',expand=True)
        scroll.pack(side='right',fill='y')
        self.tree.bind('<<TreeviewSelect>>',self.show_selection)
        ttk.Label(outer,textvariable=self.detail,foreground='#536078',wraplength=990).pack(anchor='w',pady=(8,12))
        actions=ttk.Frame(outer)
        actions.pack(fill='x')
        self.upload_button=ttk.Button(actions,text='转换并上传',command=lambda:self.start(True))
        self.upload_button.pack(side='left')
        self.export_button=ttk.Button(actions,text='仅转换并导出…',command=lambda:self.start(False))
        self.export_button.pack(side='left',padx=10)
        self.cancel_button=ttk.Button(actions,text='取消',command=self.cancel,state='disabled')
        self.cancel_button.pack(side='right')
        ttk.Label(outer,textvariable=self.status,wraplength=990).pack(anchor='w',pady=(14,6))
        self.progress=ttk.Progressbar(outer,mode='determinate',maximum=100)
        self.progress.pack(fill='x')
        self.log=tk.Text(outer,height=7,font=('Microsoft YaHei UI',9),bg='#f8fafc',fg='#536078',relief='solid',bd=1,wrap='word',state='disabled')
        self.log.pack(fill='x',pady=(12,0))
        self._log('上传会重启设备进入专用模式；整批文件校验成功后保存，并再次重启加载新资源。')
        self._log('首次使用需烧录当前工程的新固件。照片目录沿用 /imges；MP3 自动转 WAV，同名 LRC 原样上传。')
        self.refresh_ports()
        self._options()
        self._buttons()
        root.protocol('WM_DELETE_WINDOW',self.on_close)
        root.after(100,self._pump)

    def _kind(self): return next(key for key,value in KINDS.items() if value==self.kind.get())

    def _options(self):
        if self.busy: return
        kind=self._kind()
        self.slot_box.configure(state='readonly' if kind=='wallpaper' else 'disabled')
        self.fps_box.configure(state='readonly' if kind=='video' else 'disabled')
        self.detail.set({'wallpaper':'壁纸会中心裁切为 480×480。可多选，按所选位置依次放入最多三个壁纸位置。',
                         'photo':'照片保留完整画面，按比例缩放至 480×450，空白区域补黑边。',
                         'music':'MP3 自动转 WAV；LRC 原样上传、不转码，也可自动带上 MP3 旁边的同名歌词。',
                         'video':'MP4 转为 320×240 视频和同名音轨；播放时保留原有抗锯齿。'}[kind])

    def refresh_ports(self):
        previous=self.port_map.get(self.port.get())
        ports=list(list_ports.comports())
        self.port_map={f'{p.device} · {p.description}':p.device for p in ports}
        self.port_box.configure(values=list(self.port_map))
        selected=next((label for label,device in self.port_map.items() if device==previous),None)
        if not selected:
            usb=next((p.device for p in ports if p.vid is not None),None)
            selected=next((label for label,device in self.port_map.items() if device==usb),'')
        self.port.set(selected)
        self.connection.set('上传速率 921600' if selected else '请连接设备并选择串口')
        self._buttons()

    def add_files(self):
        kind=self._kind()
        filters={'wallpaper':[('图片','*.png *.jpg *.jpeg')], 'photo':[('照片','*.png *.jpg *.jpeg')],
                 'music':[('音乐和歌词','*.mp3 *.lrc')], 'video':[('视频','*.mp4')]}[kind]
        paths=filedialog.askopenfilenames(parent=self.root,title='选择'+KINDS[kind],filetypes=filters)
        if not paths: return
        try:
            additions=[]
            for index,path in enumerate(paths):
                slot=int(self.slot.get())+(index if kind=='wallpaper' else 0)
                if kind=='wallpaper' and slot>3: raise ValueError('壁纸只有三个位置，请调整起始位置或减少选择的文件')
                additions.append(MediaJob(kind,Path(path),slot,int(self.fps.get())))
            self.jobs=plan_jobs(self.jobs+additions)
            self._render_jobs()
        except (ValueError,OSError) as exc: messagebox.showerror('无法添加素材',str(exc),parent=self.root)

    def _render_jobs(self):
        for row in self.tree.get_children(): self.tree.delete(row)
        for index,job in enumerate(self.jobs):
            targets=predicted_targets(job)
            spec={'wallpaper':'480×480 · BIN','photo':'480×450 · BIN','music':'44100 Hz · WAV/LRC','video':f'320×240 · {job.fps} FPS'}[job.kind]
            if job.kind=='music' and job.source.suffix.lower()=='.lrc':
                spec='LRC · 原样上传'
            self.tree.insert('', 'end',iid=str(index),values=(KINDS[job.kind],job.source.name,' + '.join(targets),spec))
        self._buttons()

    def show_selection(self,_=None):
        rows=self.tree.selection()
        if rows: self.detail.set(str(self.jobs[int(rows[0])].source))

    def remove_selected(self):
        selected={int(row) for row in self.tree.selection()}
        self.jobs=[job for index,job in enumerate(self.jobs) if index not in selected]
        self._render_jobs()

    def clear(self): self.jobs=[]; self._render_jobs()

    def _buttons(self):
        idle=not self.busy
        for widget in (self.add_button,self.remove_button,self.clear_button,self.refresh_button):
            widget.configure(state='normal' if idle else 'disabled')
        self.kind_box.configure(state='readonly' if idle else 'disabled')
        self.port_box.configure(state='readonly' if idle else 'disabled')
        if not idle:
            self.slot_box.configure(state='disabled'); self.fps_box.configure(state='disabled')
        lyrics_only=bool(self.jobs) and all(job.kind=='music' and job.source.suffix.lower()=='.lrc' for job in self.jobs)
        self.export_button.configure(text='仅导出 LRC…' if lyrics_only else '仅转换并导出…',
                                     state='normal' if idle and self.jobs else 'disabled')
        self.upload_button.configure(text='直接上传 LRC' if lyrics_only else '转换并上传',
                                     state='normal' if idle and self.jobs and self.port.get() in self.port_map else 'disabled')
        self.cancel_button.configure(state='normal' if self.busy and not self.committing else 'disabled')

    def start(self,upload):
        if self.busy: return
        try: jobs=plan_jobs(self.jobs)
        except ValueError as exc:
            messagebox.showerror('素材队列',str(exc),parent=self.root); return
        directory=None
        if not upload:
            directory=filedialog.askdirectory(parent=self.root,title='选择导出目录（会自动创建素材子目录）',initialdir=application_dir())
            if not directory: return
        port=self.port_map.get(self.port.get())
        if upload and not port:
            messagebox.showerror('设备未连接','请选择设备的 USB 串口',parent=self.root); return
        self.busy=True; self.committing=False; self.cancel_event.clear()
        self.progress.configure(value=0)
        self.transfer_start=0
        self._buttons()
        threading.Thread(target=self._worker,args=(jobs,port,Path(directory) if directory else None),daemon=True).start()

    def _worker(self,jobs,port,directory):
        try:
            with tempfile.TemporaryDirectory(prefix='esp32-media-') as temp:
                files=prepare_jobs(jobs,Path(temp),self.cancel_event,self.events.put)
                self.events.put(dict(stage='log',message=f'素材准备完成，共 {len(files)} 个输出文件'))
                if directory:
                    if self.cancel_event.is_set(): raise UploadCancelled('已取消导出')
                    self.events.put(dict(stage='commit',message='正在保存素材'))
                    export_files(files,directory)
                    message=f'素材已导出到 {directory}'
                else:
                    with SerialUploader(port,cancel_event=self.cancel_event,progress=self.events.put,
                                        log=lambda text:self.events.put(dict(stage='log',message=text))) as uploader:
                        uploader.upload(files)
                    message='全部素材已保存到 SD 卡，设备正在重启加载'
            self.events.put(dict(stage='done',status='ok',message=message))
        except UploadCancelled as exc:
            self.events.put(dict(stage='done',status='cancelled',message=str(exc)))
        except Exception as exc:
            self.events.put(dict(stage='done',status='error',message=str(exc)))

    def _log(self,text):
        self.log.configure(state='normal')
        self.log.insert('end',str(text)+'\n'); self.log.see('end')
        if int(self.log.index('end-1c').split('.')[0])>300: self.log.delete('1.0','80.0')
        self.log.configure(state='disabled')

    def _pump(self):
        try:
            while True:
                event=self.events.get_nowait(); stage=event['stage']
                if stage=='log': self._log(event['message']); continue
                if stage=='done':
                    self.busy=False; self.committing=False
                    self.progress.stop(); self.progress.configure(mode='determinate',value=100 if event['status']=='ok' else 0)
                    self.status.set(event['message']); self._log(event['message'])
                    self._buttons(); self._options()
                    if self.closing: self.root.destroy(); return
                    if event['status']=='error': messagebox.showerror('任务未完成',event['message'],parent=self.root)
                    continue
                if stage in ('convert','connect'):
                    self.progress.configure(mode='indeterminate'); self.progress.start(20)
                    self.status.set(event.get('message','素材已准备，开始上传'))
                elif stage in ('upload','verify'):
                    self.progress.stop(); self.progress.configure(mode='determinate')
                    done,total=event['done'],event['total']
                    self.progress.configure(value=done*100/total if total else 100)
                    action='上传' if stage=='upload' else '校验'
                    speed=''
                    if stage=='upload':
                        if not self.transfer_start: self.transfer_start=time.monotonic()
                        elapsed=max(time.monotonic()-self.transfer_start,0.1)
                        average=event.get('aggregate_done',0)/elapsed/1024
                        speed=f'  ·  {average:.1f} KiB/s'
                    self.status.set(f'{action} {event["path"]}  ·  {done/1024/1024:.2f} / {total/1024/1024:.2f} MiB{speed}')
                elif stage=='commit':
                    self.committing=True; self._buttons()
                    self.status.set(event['message']); self.progress.configure(mode='indeterminate'); self.progress.start(20)
        except queue.Empty: pass
        self.root.after(100,self._pump)

    def cancel(self):
        if self.busy and not self.committing:
            self.cancel_event.set(); self.status.set('正在取消并清理未提交素材…'); self.cancel_button.configure(state='disabled')

    def on_close(self):
        if self.busy:
            self.closing=True
            self.cancel()
            if self.committing: self.status.set('正在提交，完成后自动关闭…')
        else: self.root.destroy()

def main():
    root=tk.Tk()
    UploaderApp(root)
    root.mainloop()

if __name__=='__main__': main()
