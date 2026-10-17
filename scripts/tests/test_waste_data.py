"""Persistent schedule migration: copies data, never removes or overwrites it."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('waste_data',Path(__file__).resolve().parents[1]/'waste_data.py')
data=importlib.util.module_from_spec(spec);spec.loader.exec_module(data)
PAYLOAD=b'{"timezone":"UTC","pickups":[]}'

class WasteDataTests(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory();self.addCleanup(temporary.cleanup)
        self.root=Path(temporary.name);self.home=Path('/home/operator')
        self.source=self.root/'home/operator/infohub/data/garbage_schedule.json'
        self.source.parent.mkdir(parents=True);self.source.write_bytes(PAYLOAD)
        self.destination=self.root/data.DEFAULT_SCHEDULE.relative_to('/')
        self.stage=self.root/'stage';self.stage.mkdir()
        self.validated=[]
    def validate(self,path):
        self.validated.append(path)
        document=json.loads(path.read_text())
        if not isinstance(document.get('pickups'),list):raise ValueError('invalid schedule')
    def plan(self,configured='/home/operator/infohub/data/garbage_schedule.json',explicit=None):
        return data.prepare(self.root,self.home,self.stage,configured,explicit,os.getuid(),os.getgid(),self.validate)
    def create_destination(self,payload=PAYLOAD):
        self.destination.parent.mkdir(parents=True,mode=0o700)
        self.destination.write_bytes(payload)

    def test_migration_preserves_source_and_validates_exact_copy_as_account(self):
        original=self.source.stat();plan=self.plan()
        self.assertFalse(self.destination.exists())
        self.assertEqual(list(self.stage.iterdir()),[])
        with patch.object(data.os,'chown'):plan.apply()
        self.assertEqual(self.destination.read_bytes(),PAYLOAD)
        self.assertEqual(self.source.read_bytes(),PAYLOAD)
        self.assertEqual(self.source.stat().st_ino,original.st_ino)
        self.assertEqual(self.source.stat().st_mtime_ns,original.st_mtime_ns)
        self.assertEqual(self.destination.stat().st_mode&0o777,0o600)
        self.assertEqual(self.destination.parent.stat().st_mode&0o777,0o700)
        self.assertEqual(self.validated[-1],self.destination)
        report=plan.report()
        self.assertTrue(report['migration']['copied'] and report['migration']['source_preserved'])
        self.assertEqual(report['sha256'],hashlib.sha256(PAYLOAD).hexdigest())

    def test_conflicting_destination_rejected_before_copy(self):
        different=b'{"timezone":"Europe/Warsaw","pickups":[]}'
        self.create_destination(different)
        with self.assertRaisesRegex(RuntimeError,'Conflicting'):self.plan()
        self.assertEqual(self.destination.read_bytes(),different)
        self.assertEqual(self.source.read_bytes(),PAYLOAD)

    def test_identical_retained_copy_is_reused_on_retry(self):
        self.create_destination();inode=self.destination.stat().st_ino
        plan=self.plan();plan.apply()
        self.assertFalse(plan.copied)
        self.assertEqual(self.destination.stat().st_ino,inode)

    def test_source_change_after_preflight_aborts_without_destination(self):
        plan=self.plan();self.source.write_bytes(b'{"timezone":"Europe/Warsaw","pickups":[]}')
        with self.assertRaisesRegex(RuntimeError,'changed since preflight'):plan.apply()
        self.assertFalse(self.destination.exists())

    def test_destination_race_does_not_overwrite_data(self):
        plan=self.plan();different=b'{"timezone":"Europe/Warsaw","pickups":[]}'
        self.create_destination(different)
        with self.assertRaisesRegex(RuntimeError,'appeared with different'):plan.apply()
        self.assertEqual(self.destination.read_bytes(),different)
        self.assertEqual(list(self.destination.parent.glob('.schedule-*')),[])

    def test_validation_failure_leaves_original_and_no_new_file(self):
        self.source.write_bytes(b'{')
        with self.assertRaises(ValueError):self.plan()
        self.assertEqual(self.source.read_bytes(),b'{')
        self.assertFalse(self.destination.exists())

    def test_updated_active_schedule_is_not_reset_from_old_backup(self):
        updated=b'{"timezone":"Europe/Warsaw","pickups":[]}'
        self.create_destination(updated)
        plan=self.plan(configured=str(data.DEFAULT_SCHEDULE));plan.apply()
        self.assertIsNone(plan.source)
        self.assertEqual(self.destination.read_bytes(),updated)
        self.assertEqual(self.source.read_bytes(),PAYLOAD)

    def test_missing_active_file_is_not_silently_resurrected_from_backup(self):
        with self.assertRaises(FileNotFoundError):self.plan(configured=str(data.DEFAULT_SCHEDULE))

    def test_symlink_and_oversized_source_are_rejected(self):
        self.source.unlink();self.source.symlink_to('/etc/passwd')
        with self.assertRaises(OSError):self.plan()
        self.source.unlink();self.source.write_bytes(b'x'*(data.MAXIMUM_BYTES+1))
        with self.assertRaises(ValueError):self.plan()

    def test_custom_schedule_remains_external(self):
        custom=self.root/'custom.json';custom.write_bytes(PAYLOAD)
        plan=self.plan(explicit='/custom.json');plan.apply()
        self.assertEqual(plan.destination,custom)
        self.assertIsNone(plan.source)
        self.assertFalse(self.destination.exists())

    def test_failed_publish_retains_source_and_cleans_temporary_file(self):
        plan=self.plan()
        with patch.object(data.os,'chown'),patch.object(data.os,'link',side_effect=OSError('disk error')):
            with self.assertRaisesRegex(OSError,'disk error'):plan.apply()
        self.assertFalse(self.destination.exists())
        self.assertEqual(self.source.read_bytes(),PAYLOAD)
        self.assertEqual(list(self.destination.parent.glob('.schedule-*')),[])

    def test_cleanup_preserves_verified_local_backup_before_removing_old_schedule(self):
        plan=self.plan()
        with patch.object(data.os,'chown'):
            plan.apply();plan.cleanup.apply()
        self.assertFalse(self.source.exists())
        self.assertFalse(self.source.parent.parent.exists())
        self.assertEqual(plan.cleanup.backup.read_bytes(),PAYLOAD)
        self.assertEqual(self.destination.read_bytes(),PAYLOAD)
        self.assertEqual(plan.cleanup.backup.stat().st_mode&0o777,0o600)

    def test_cleanup_retains_unknown_files_and_reports_remaining_directory(self):
        unknown=self.source.parent.parent/'notes.txt';unknown.write_text('keep')
        plan=self.plan()
        with patch.object(data.os,'chown'):
            plan.apply();plan.cleanup.apply()
        self.assertEqual(unknown.read_text(),'keep')
        self.assertTrue(plan.cleanup.report()['directory_remaining'])

    def test_cleanup_backup_conflict_does_not_remove_source(self):
        self.create_destination()
        backup=self.destination.parent/'garbage_schedule.infohub-backup.json'
        backup.write_bytes(b'different')
        with self.assertRaisesRegex(RuntimeError,'Conflicting'):self.plan()
        self.assertEqual(self.source.read_bytes(),PAYLOAD)

    def test_cleanup_source_change_does_not_remove_source(self):
        plan=self.plan();self.source.write_bytes(b'changed')
        with self.assertRaisesRegex(RuntimeError,'changed'):plan.cleanup.apply()
        self.assertEqual(self.source.read_bytes(),b'changed')

    def test_cleanup_failed_backup_does_not_remove_source(self):
        plan=self.plan()
        with patch.object(data.os,'chown'),patch.object(data.os,'link',side_effect=OSError('disk full')):
            with self.assertRaises(OSError):plan.cleanup.apply()
        self.assertEqual(self.source.read_bytes(),PAYLOAD)

    def test_cleanup_repeat_keeps_backup_and_active_data(self):
        plan=self.plan()
        with patch.object(data.os,'chown'):
            plan.apply();plan.cleanup.apply()
        again=self.plan(configured=str(data.DEFAULT_SCHEDULE))
        again.cleanup.apply()
        self.assertEqual(again.cleanup.backup.read_bytes(),PAYLOAD)
        self.assertEqual(self.destination.read_bytes(),PAYLOAD)
