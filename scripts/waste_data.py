"""Copy a legacy schedule into dedicated persistent storage without deleting data."""
import hashlib
import os
from pathlib import Path
import stat
import tempfile

DEFAULT_SCHEDULE=Path('/var/lib/kafel-waste/garbage_schedule.json')
MAXIMUM_BYTES=256*1024


def read_schedule(path):
    fd=os.open(path,os.O_RDONLY|os.O_CLOEXEC|os.O_NONBLOCK|os.O_NOFOLLOW)
    with os.fdopen(fd,'rb') as stream:
        before=os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or before.st_size>MAXIMUM_BYTES:
            raise ValueError('Schedule must be a regular file of at most 256 KiB')
        data=stream.read(MAXIMUM_BYTES+1)
        after=os.fstat(stream.fileno())
    if len(data)>MAXIMUM_BYTES or (before.st_ino,before.st_size,before.st_mtime_ns,before.st_ctime_ns)!=(after.st_ino,after.st_size,after.st_mtime_ns,after.st_ctime_ns):
        raise RuntimeError('Schedule changed during read')
    return data


def safe_parents(root,parent):
    path=root
    for part in parent.relative_to(root).parts:
        path=path/part
        if path.is_symlink():raise RuntimeError('Refusing schedule directory symlink: '+str(path))
        if path.exists() and not path.is_dir():raise RuntimeError('Not a schedule directory: '+str(path))


class SchedulePlan:
    def __init__(self,root,destination,data,validate,uid,gid,source=None,needs_copy=False):
        self.root,self.destination,self.data=root,destination,data
        self.validate,self.uid,self.gid=validate,uid,gid
        self.source,self.needs_copy=source,needs_copy
        self.copied=False
        self.sha256=hashlib.sha256(data).hexdigest()

    def report(self):
        result={'path':'/'+str(self.destination.relative_to(self.root)),'sha256':self.sha256}
        if self.source:
            result['migration']={'source':'/'+str(self.source.relative_to(self.root)),
                'copied':self.copied,'source_preserved':True,'backup_entry':'waste-schedule-original.json'}
        return result

    def apply(self):
        if self.source and read_schedule(self.source)!=self.data:
            raise RuntimeError('Source schedule changed since preflight; retry deployment')
        if self.needs_copy:
            parent=self.destination.parent
            safe_parents(self.root,parent)
            if not parent.exists():
                parent.parent.mkdir(parents=True,exist_ok=True)
                parent.mkdir(mode=0o700)
                os.chown(parent,self.uid,self.gid)
            if parent.stat().st_uid!=self.uid or parent.stat().st_mode&0o077:
                raise RuntimeError('Schedule directory must be private and owned by the service account')
            fd,name=tempfile.mkstemp(prefix='.schedule-',dir=parent)
            try:
                with os.fdopen(fd,'wb') as stream:
                    stream.write(self.data);stream.flush();os.fsync(stream.fileno())
                os.chmod(name,0o600);os.chown(name,self.uid,self.gid)
                try:
                    # Publish atomically without overwriting an independently created file.
                    os.link(name,self.destination,follow_symlinks=False)
                    self.copied=True
                except FileExistsError:
                    if read_schedule(self.destination)!=self.data:
                        raise RuntimeError('Destination schedule appeared with different data; refusing overwrite')
                directory=os.open(parent,os.O_RDONLY|os.O_DIRECTORY|os.O_CLOEXEC)
                try:os.fsync(directory)
                finally:os.close(directory)
            finally:
                Path(name).unlink(missing_ok=True)
        if read_schedule(self.destination)!=self.data:
            raise RuntimeError('Schedule copy verification failed')
        self.validate(self.destination)
        if self.source and read_schedule(self.source)!=self.data:
            raise RuntimeError('Original schedule changed during migration')
        # A successfully copied data file is retained even if service rollback
        # later restores the old configuration. Retry safely reuses this copy.


def prepare(root,home,stage,configured,explicit,uid,gid,validate):
    legacy=home/'infohub/data/garbage_schedule.json'
    requested=Path(explicit or configured or DEFAULT_SCHEDULE)
    migrating=(requested==legacy or
               (requested==DEFAULT_SCHEDULE and (configured==str(legacy) or
                (not configured and (root/legacy.relative_to('/')).exists()))))
    if migrating:requested=DEFAULT_SCHEDULE
    destination=root/requested.relative_to('/')
    safe_parents(root,destination.parent)
    source=root/legacy.relative_to('/') if migrating else None
    if source:
        safe_parents(root,source.parent)
        data=read_schedule(source)
        validate(source)
        needs_copy=not destination.exists() and not destination.is_symlink()
        if not needs_copy:
            if read_schedule(destination)!=data:
                raise RuntimeError('Conflicting schedules at source and destination; refusing overwrite')
            validate(destination)
        else:
            # Validate the exact bytes we will install, as the real account,
            # while the running service and configuration are still untouched.
            fd,name=tempfile.mkstemp(prefix='waste-candidate-',suffix='.json',dir=stage)
            try:
                with os.fdopen(fd,'wb') as stream:stream.write(data)
                os.chmod(name,0o600);os.chown(name,uid,gid)
                validate(Path(name))
            finally:Path(name).unlink(missing_ok=True)
    else:
        data=read_schedule(destination)
        validate(destination)
        needs_copy=False
    return SchedulePlan(root,destination,data,validate,uid,gid,source,needs_copy)
