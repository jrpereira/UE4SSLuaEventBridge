from datetime import datetime, timezone
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import json

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from runtime_evidence import from_log, from_bridge


class RuntimeEvidenceTests(unittest.TestCase):
    def test_live_query_rejects_cached_reply_and_changed_process(self):
        for changed in [False,True]:
            with tempfile.TemporaryDirectory() as tmp:
                folder=Path(tmp)
                session={'pid':123,'started_at':'2026-09-18T10:00:00Z'}
                (folder/'response.json').write_text(json.dumps({'id':'old','ok':True,'protocol':3,'result':'0.0.1'}))
                def respond(_):
                    request=json.loads((folder/'request.json').read_text())
                    (folder/'request.json').unlink()
                    (folder/'response.json').write_text(json.dumps({'id':request['id'],'ok':True,'protocol':3,'result':'0.3.4-rc.2'}))
                states=iter([session,None if changed else session])
                with patch('runtime_evidence.time.sleep',respond):
                    result=from_bridge(folder,session,lambda:next(states))
                if changed:
                    self.assertIsNone(result)
                else:
                    self.assertEqual(result['version'],'0.3.4-rc.2')
                    self.assertIsNone(result['loaded_file_hashes'])

    def test_existing_request_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp)
            (folder/'request.json').write_text('another client request')
            with self.assertRaises(FileExistsError):
                from_bridge(folder,{'pid':1},lambda:{'pid':1})
            self.assertEqual((folder/'request.json').read_text(),'another client request')

    def test_current_log_reports_version_without_claiming_loaded_hashes(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp)/'UE4SS.log'
            log.write_text('[2026-09-18 10:00:01.1234567] Console created\n'
                           '[2026-09-18 10:00:12.1234567] [Lua] [ExtendedControls] Loaded v0.3.53. Auto-initialization deferred\n'
                           '[2026-09-18 10:00:13.1] [Lua] [ModMenuDecorator] 0.1.31-rc.1 ready\n')
            session = {'pid':123,'started_at':'2026-09-18T10:00:00+00:00'}
            now = datetime(2026,9,18,11,tzinfo=timezone.utc)
            ec = from_log(log,'ExtendedControls',session,timezone.utc,now)
            self.assertEqual(ec['version'], '0.3.53')
            self.assertIsNone(ec['loaded_file_hashes'])
            self.assertEqual(from_log(log,'ModMenuDecorator',session,timezone.utc,now)['version'],'0.1.31-rc.1')
            self.assertIsNone(from_log(log,'UE4SSLuaEventBridge',session,timezone.utc,now))
            for start in ['2026-09-17T10:00:00Z','2026-09-18T10:01:00Z']:
                self.assertIsNone(from_log(log,'ExtendedControls',{'pid':123,'started_at':start},timezone.utc,now))


if __name__ == '__main__':
    unittest.main()
