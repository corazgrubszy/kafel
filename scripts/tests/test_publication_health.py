"""Readiness requires an accepted snapshot, not merely a started producer."""
import importlib.util
import itertools
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('publication_install',Path(__file__).resolve().parents[1]/'install.py')
install=importlib.util.module_from_spec(spec);spec.loader.exec_module(install)


class PublicationHealthTests(unittest.TestCase):
    def exercise(self, name, logs, restarted=False):
        reads=[]
        def command(*args, **kwargs):
            out=''
            if args[:2]==('systemctl','show'):
                out='124' if restarted and reads else '123'
            elif args[0]=='journalctl':
                out=logs[min(len(reads),len(logs)-1)];reads.append(args)
            return subprocess.CompletedProcess(args,0,out,'')
        job=install.Installer('/tmp/kafel-publication-unit-test','operator',command=command)
        with patch.object(install.time,'sleep'),patch.object(install.time,'monotonic',side_effect=itertools.count()):
            job.health((name+'.service',),(name,))
        self.assertTrue(all('_PID=123' in call for call in reads))
        return job.health_report[name+'.service'],len(reads)

    def log(self,name,extra=''):
        return 'READY '+name+': publisher started (DASH/5)\n'+extra

    def accepted(self,name):
        return 'PUBLISHED '+name+': DASH/5 snapshot accepted\n'

    def test_started_but_rejected_producer_is_not_ready(self):
        for name in ('kafel-weather','kafel-waste'):
            with self.subTest(name=name),self.assertRaisesRegex(RuntimeError,'acceptance missing'):
                self.exercise(name,[self.log(name,'waste schedule loaded: 1 pickup days\ndashboard publish unavailable: invalid_template\n')])

    def test_waits_for_acceptance_and_records_it(self):
        name='kafel-waste';started=self.log(name,'waste schedule loaded: 1 pickup days\n')
        state,reads=self.exercise(name,[started,started+self.accepted(name)])
        self.assertEqual(reads,2)
        self.assertTrue(state['readiness'] and state['publication_accepted'])
        self.assertEqual(state['source'],'loaded')

    def test_source_error_snapshot_is_valid_readiness(self):
        name='kafel-weather'
        state,_=self.exercise(name,[self.log(name,'weather refresh failed: offline\n'+self.accepted(name))])
        self.assertTrue(state['readiness'])
        self.assertEqual(state['source'],'error')

    def test_restart_during_wait_is_failure(self):
        with self.assertRaisesRegex(RuntimeError,'restarted'):
            self.exercise('kafel-waste',[self.log('kafel-waste')],restarted=True)

    def test_acceptance_without_ready_marker_is_failure(self):
        with self.assertRaisesRegex(RuntimeError,'Readiness marker missing'):
            self.exercise('kafel-waste',[self.accepted('kafel-waste')])

    def test_old_error_snapshot_cannot_hide_rejected_fresh_data(self):
        name='kafel-weather'
        log=self.log(name,self.accepted(name)+'weather cache updated\ndashboard publish unavailable: invalid_template\n')
        with self.assertRaisesRegex(RuntimeError,'acceptance missing'):
            self.exercise(name,[log])

    def test_waits_for_ack_after_latest_source_result(self):
        name='kafel-weather'
        log=self.log(name,self.accepted(name)+'weather cache updated\n')
        state,reads=self.exercise(name,[log,log+self.accepted(name)])
        self.assertEqual(reads,2)
        self.assertEqual(state['source'],'updated')

    def test_latest_source_error_is_reported_after_recovery(self):
        name='kafel-weather'
        log=self.log(name,'weather cache updated\n'+self.accepted(name)+'weather refresh failed: offline\n'+self.accepted(name))
        state,_=self.exercise(name,[log])
        self.assertEqual(state['source'],'error')

    def test_initial_ack_without_source_result_is_not_ready(self):
        name='kafel-weather'
        with self.assertRaisesRegex(RuntimeError,'acceptance missing'):
            self.exercise(name,[self.log(name,self.accepted(name))])
