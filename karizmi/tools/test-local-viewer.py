"""Interactive local acceptance check. Requests normal firewall-helper UAC approvals.
Runs packaged Teacher/Student, pairs them on this PC, and checks viewer opening.
This does not replace a two-computer LAN test.
"""
from pathlib import Path
from datetime import datetime,timezone
exec(Path(__file__).with_name('inspect-ui.py').read_text().split('parser = argparse.ArgumentParser()')[0])

root=Path(__file__).resolve().parents[1]
output=root/'build'/'local-viewer'; output.mkdir(parents=True,exist_ok=True)
processes=[]; handles={}; expected_forced=set()
started=datetime.now(timezone.utc).isoformat()
def wait_for(function,seconds=30):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        result=function()
        if result:return result
        time.sleep(.1)
    raise RuntimeError('Timed out: '+function.__name__)
def find(process,kind):
    return next((h for h in windows(process.pid,True) if window_text(h,True)==kind),None)
def click(window,identifier): user.SendMessageW(window,0x111,identifier,user.GetDlgItem(window,identifier) or 0)
def read_control(window,identifier):
    buffer=ct.create_unicode_buffer(2048)
    user.SendMessageW(user.GetDlgItem(window,identifier),0xD,len(buffer),ct.addressof(buffer))
    return buffer.value
def write_control(window,identifier,value):
    buffer=ct.create_unicode_buffer(value)
    user.SendMessageW(user.GetDlgItem(window,identifier),0xC,0,ct.addressof(buffer))
def connected(window):
    status=read_control(window,111)
    return status.startswith(('Connection tested','VIEW ONLY','CONTROL ON'))
def foreground(window):
    kernel=ct.WinDLL('kernel32');kernel.GetCurrentThreadId.restype=wt.DWORD
    user.GetForegroundWindow.restype=wt.HWND
    user.AttachThreadInput.argtypes=[wt.DWORD,wt.DWORD,wt.BOOL]
    user.SetForegroundWindow.argtypes=[wt.HWND];user.SetFocus.argtypes=[wt.HWND]
    user.BringWindowToTop.argtypes=[wt.HWND]
    current=kernel.GetCurrentThreadId();ids=set()
    for handle in (user.GetForegroundWindow(),window):
        process_id=wt.DWORD();thread=user.GetWindowThreadProcessId(handle,ct.byref(process_id))
        if thread and thread!=current:ids.add(thread)
    attached=[]
    try:
        for thread in ids:
            if user.AttachThreadInput(current,thread,True):attached.append(thread)
        user.BringWindowToTop(window);user.SetForegroundWindow(window);user.SetFocus(window)
    finally:
        for thread in attached:user.AttachThreadInput(current,thread,False)
try:
    student=subprocess.Popen([str(root/'out/Student/KarizmiStudent.exe')]); processes.append(student)
    def accept_notice():
        for h in windows(student.pid):
            if 'monitoring notice' in window_text(h).lower():
                user.PostMessageW(h,0x111,1,user.GetDlgItem(h,1)); return True
    wait_for(accept_notice)
    student_window=wait_for(lambda:find(student,'KarizmiMain')); handles[student.pid]=(student_window,108)
    user.PostMessageW(student_window,0x8007,1,0x203)
    teacher=subprocess.Popen([str(root/'out/Teacher/KarizmiTeacher.exe')]); processes.append(teacher)
    teacher_window=wait_for(lambda:find(teacher,'KarizmiClassroom')); handles[teacher.pid]=(teacher_window,208)
    click(student_window,106)
    wait_for(lambda:'Rule configured' in read_control(student_window,111),90)
    print('Student firewall ready',flush=True)
    click(teacher_window,206)
    wait_for(lambda:'Rule configured' in read_control(teacher_window,207),90)
    print('Teacher firewall ready',flush=True)
    wait_for(lambda:user.SendMessageW(user.GetDlgItem(teacher_window,201),0x18B,0,0)>0)
    if len(read_control(student_window,101))==8:
        if find(teacher,'KarizmiMain'):click(teacher_window,205)
        user.SendMessageW(user.GetDlgItem(teacher_window,201),0x186,0,0)
        write_control(teacher_window,202,read_control(student_window,101)); click(teacher_window,203)
        session=wait_for(lambda:find(teacher,'KarizmiMain'))
        wait_for(lambda:read_control(session,113) and read_control(session,113)==read_control(student_window,113) and user.IsWindowVisible(user.GetDlgItem(session,109)) and user.IsWindowVisible(user.GetDlgItem(student_window,109)))
        click(session,109); click(student_window,109)
    else:
        session=wait_for(lambda:find(teacher,'KarizmiMain'))
    wait_for(lambda:connected(session),30)
    remote=wait_for(lambda:find(teacher,'VNCMDI_Window'),30)
    user.GetPropW.argtypes=[wt.HWND,wt.LPCWSTR];user.GetPropW.restype=wt.HANDLE
    def property_value(window,name):return int(user.GetPropW(window,name) or 0)
    # Actual owner-drawn tile double-click notification.
    user.PostMessageW(teacher_window,0x111,220 | (5<<16),user.GetDlgItem(teacher_window,220))
    wait_for(lambda:user.IsWindowVisible(remote))
    capture(remote,output/'remote-open.png')
    foreground(remote)
    print('PASS: pairing, viewer creation, tile double-click opens screen',flush=True)
    # Invoke the same per-tile action used by the context menu; avoid input injection.
    user.SendMessageW(teacher_window,0x111,240,0)
    time.sleep(.5); capture(remote,output/'remote-fullscreen.png')
    user.GetWindowLongW.argtypes=[wt.HWND,ct.c_int]
    wait_for(lambda:not (user.GetWindowLongW(remote,-16)&0x00C00000),10)
    print('PASS: context-menu full screen removes window caption',flush=True)
    click(teacher_window,260)
    wait_for(lambda:property_value(student_window,'KarizmiControlOn')==1 and property_value(session,'KarizmiControlOn')==1)
    for transition in range(4):
        user.SendMessageW(remote,0x112,113,0) # Native viewer full-screen toggle.
        time.sleep(.3)
        assert property_value(student_window,'KarizmiControlOn')==1,'Full-screen transition revoked Student control'
    print('PASS: Student-acknowledged control survives repeated full-screen transitions',flush=True)
    keys_before=property_value(student_window,'KarizmiRemoteKeyEvents')
    foreground(remote)
    def shortcut():
        user.keybd_event(0x11,0,0,0);user.keybd_event(0x10,0,0,0)
        user.keybd_event(0x7B,0,0,0);user.keybd_event(0x7B,0,2,0)
        user.keybd_event(0x10,0,2,0);user.keybd_event(0x11,0,2,0)
    shortcut();wait_for(lambda:property_value(student_window,'KarizmiControlOn')==0)
    shortcut();wait_for(lambda:property_value(student_window,'KarizmiControlOn')==1)
    time.sleep(.5)
    assert property_value(student_window,'KarizmiRemoteKeyEvents')==keys_before,'Shortcut key events reached Student'
    print('PASS: physical Ctrl+Shift+F12 toggles acknowledged control; no shortcut key events reach Student',flush=True)
    user.SendMessageW(teacher_window,0x8007,1,0x203) # Bring classroom forward.
    user.SetForegroundWindow.argtypes=[wt.HWND];user.SetForegroundWindow(teacher_window)
    click(teacher_window,220)
    # Same application grid keeps its selected control target; an unrelated
    # top-level test window below must revoke control.
    user.CreateWindowExW.argtypes=[wt.DWORD,wt.LPCWSTR,wt.LPCWSTR,wt.DWORD,ct.c_int,ct.c_int,ct.c_int,ct.c_int,wt.HWND,wt.HMENU,wt.HINSTANCE,ct.c_void_p]
    user.CreateWindowExW.restype=wt.HWND;user.DestroyWindow.argtypes=[wt.HWND]
    external=user.CreateWindowExW(0,'STATIC','Karizmi test external focus',0x10CF0000,50,50,400,200,None,None,None,None)
    try:
        foreground(external)
        wait_for(lambda:property_value(student_window,'KarizmiControlOn')==0)
    finally:user.DestroyWindow(external)
    print('PASS: unrelated foreground window revokes control',flush=True)
    click(teacher_window,240)
    generation=wait_for(lambda:property_value(session,'KarizmiSessionGeneration'))
    click(teacher_window,280)
    wait_for(lambda:property_value(session,'KarizmiSessionGeneration')>generation and connected(session),45)
    remote=wait_for(lambda:find(teacher,'VNCMDI_Window'))
    assert property_value(student_window,'KarizmiControlOn')==0 and not read_control(student_window,101),'Repair did not retain approval/View Only'
    click(teacher_window,240)
    print('PASS: Reconnect now repairs Connected transport without code and resumes View Only',flush=True)
    generation=property_value(session,'KarizmiSessionGeneration')
    user.SendMessageW(remote,0x10,0,1) # Simulate engine/viewer failure, not user Hide.
    wait_for(lambda:property_value(session,'KarizmiSessionGeneration')>generation and connected(session),45)
    remote=wait_for(lambda:find(teacher,'VNCMDI_Window'));click(teacher_window,240)
    assert property_value(student_window,'KarizmiControlOn')==0 and not read_control(student_window,101)
    print('PASS: automatic transport/engine-loss recovery reconnects without manual retry or code',flush=True)
    # Wait beyond the previous 30-second quiet-screen read boundary.
    deadline=time.monotonic()+35
    while time.monotonic()<deadline:
        if not find(teacher,'VNCMDI_Window'):raise RuntimeError('Viewer closed during idle test')
        time.sleep(.25)
    capture(teacher_window,output/'classroom-after-idle.png')
    user.SendMessageW(teacher_window,0x111,240,0)
    time.sleep(2)
    capture(remote,output/'remote-after-idle.png')
    print('PASS: viewer retained beyond 30-second idle boundary',flush=True)
    user.SendMessageW(remote,0x10,0,0)
    wait_for(lambda:not user.IsWindowVisible(remote))
    time.sleep(2)
    assert find(teacher,'VNCMDI_Window')==remote,'Close replaced the viewer instead of hiding it'
    assert not any(window_text(h,True)=='#32770' for h in windows(teacher.pid)),'Unexpected legacy prompt'
    user.SendMessageW(teacher_window,0x111,220|(5<<16),user.GetDlgItem(teacher_window,220))
    wait_for(lambda:user.IsWindowVisible(remote))
    print('PASS: viewer Close returns to grid; same viewer reopens without trust popup',flush=True)
    expected_forced.add(teacher.pid);teacher.terminate();teacher.wait(timeout=10)
    wait_for(lambda:'Interrupted' in read_control(student_window,111) or 'interrupted' in read_control(student_window,111),15)
    teacher=subprocess.Popen([str(root/'out/Teacher/KarizmiTeacher.exe')]);processes.append(teacher)
    teacher_window=wait_for(lambda:find(teacher,'KarizmiClassroom'));handles[teacher.pid]=(teacher_window,208)
    click(teacher_window,206);wait_for(lambda:'Rule configured' in read_control(teacher_window,207),90)
    session=wait_for(lambda:find(teacher,'KarizmiMain'))
    wait_for(lambda:connected(session),45)
    remote=wait_for(lambda:find(teacher,'VNCMDI_Window'))
    print('PASS: forced Teacher termination/relaunch reconnects without code or confirmation',flush=True)
    expected_forced.add(student.pid);student.terminate();student.wait(timeout=10)
    student=subprocess.Popen([str(root/'out/Student/KarizmiStudent.exe')]);processes.append(student)
    wait_for(accept_notice)
    student_window=wait_for(lambda:find(student,'KarizmiMain'));handles[student.pid]=(student_window,108)
    click(student_window,106);wait_for(lambda:'Rule configured' in read_control(student_window,111) or connected(student_window),90)
    wait_for(lambda:connected(student_window),45)
    print('PASS: forced Student termination/relaunch reconnects without code or confirmation',flush=True)
    click(teacher_window,208);teacher.wait(timeout=15)
    assert teacher.returncode==0,'Teacher crashed while exiting an active session'
    teacher=subprocess.Popen([str(root/'out/Teacher/KarizmiTeacher.exe')]);processes.append(teacher)
    teacher_window=wait_for(lambda:find(teacher,'KarizmiClassroom'));handles[teacher.pid]=(teacher_window,208)
    click(teacher_window,206);wait_for(lambda:'Rule configured' in read_control(teacher_window,207),90)
    session=wait_for(lambda:find(teacher,'KarizmiMain'));wait_for(lambda:connected(session),45)
    print('PASS: normal Teacher Exit preserves same-sign-in approval and relaunch resumes',flush=True)
    click(teacher_window,205)
    wait_for(lambda:len(read_control(student_window,101))==8,15)
    print('PASS: End exam revokes approval and Student generates a fresh code',flush=True)
except Exception:
    for pid,(window,command) in handles.items():
        print('Failure status',pid,read_control(window,111),read_control(window,207),flush=True)
    if 'session' in globals(): print('Teacher session',read_control(session,111),flush=True)
    raise
finally:
    # Revoke the test approvals explicitly; normal application Exit preserves them.
    if 'teacher_window' in globals() and teacher.poll() is None:click(teacher_window,205)
    if 'student_window' in globals() and student.poll() is None:click(student_window,103)
    failures=[]
    for process in reversed(processes):
        if process.pid in expected_forced:continue
        if process.poll() is None and process.pid in handles:
            window,command=handles[process.pid]; click(window,command)
        try:process.wait(timeout=15)
        except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=5)
        if process.returncode!=0:failures.append(f'Application {process.pid} exited abnormally: {process.returncode:#x}')
    if failures:raise RuntimeError('; '.join(failures))
    event_check="$events = @(Get-WinEvent -FilterHashtable @{LogName='Application'; Id=1000; StartTime=[DateTime]::Parse('"+started+"')} -ErrorAction SilentlyContinue | Where-Object { $_.Message -match 'KarizmiTeacher.exe|KarizmiStudent.exe' }); if ($events.Count) { $events | Format-List TimeCreated,Message; exit 1 }"
    result=subprocess.run(['powershell','-NoProfile','-Command',event_check],capture_output=True,text=True)
    if result.returncode:raise RuntimeError('Windows recorded a new Karizmi crash: '+result.stdout)
    print('PASS: clean application exit codes and no new Windows Application Error events',flush=True)
