"""Real HTTP tests against the compiled C process; Python standard library only."""
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import unittest
from urllib.request import Request, urlopen
from urllib.parse import urlencode
from urllib.error import HTTPError

PROJECT = Path(__file__).resolve().parents[1]

class BackendTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.temp.name) / 'famgraph'
        flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if os.environ.get('SANITIZE') else []
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror', *flags,
                        str(PROJECT/'server.c'), str(PROJECT/'family.c'), '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.data = Path(self.temp.name) / (self._testMethodName + '.tsv')
        with socket.socket() as s:
            s.bind(('127.0.0.1', 0))
            self.port = s.getsockname()[1]
        self.start()

    def start(self):
        self.process = subprocess.Popen([str(self.binary), str(self.port), str(self.data)],
                                        cwd=PROJECT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertIn('running', self.process.stdout.readline())
        self.base = 'http://127.0.0.1:' + str(self.port)

    def stop(self):
        self.process.terminate()
        stdout, stderr = self.process.communicate(timeout=6)
        self.assertEqual(self.process.returncode, 0, stderr)
        self.assertEqual(stderr, '', stderr)

    def tearDown(self):
        if self.process.poll() is None:
            self.stop()

    def call(self, path, fields=None, expected=200, trusted=True):
        headers = {'X-FamGraph': '1'} if trusted else {}
        req = Request(self.base + path, data=None if fields is None else urlencode(fields).encode(), headers=headers)
        try:
            response = urlopen(req, timeout=5)
        except HTTPError as error:
            response = error
        with response:
            self.assertEqual(response.status, expected)
            return json.load(response)

    def add(self, name, parent=None):
        return self.call('/api/root' if parent is None else '/api/child',
                         {'name':name} if parent is None else {'name':name, 'parentId':parent})['id']

    def test_relationships_and_restart(self):
        root = self.add('Raj')
        amit = self.add('Amit', root)
        priya = self.add('Priya', root)
        arjun = self.add('Arjun', amit)
        self.add('Riya', amit)
        self.add('Neha', priya)
        r = self.call('/api/relations?name=amit')
        self.assertEqual(r['parent'], ['Raj'])
        self.assertEqual(r['siblings'], ['Priya'])
        self.assertEqual(r['children'], ['Arjun', 'Riya'])
        self.assertEqual(self.call('/api/relations?id='+str(arjun))['ancestors'], ['Amit','Raj'])
        self.assertEqual(self.call('/api/relations?id='+str(root))['descendants'], ['Amit','Arjun','Riya','Priya','Neha'])
        before = self.call('/api/tree')
        self.stop(); self.start()
        self.assertEqual(self.call('/api/tree'), before)
        self.assertEqual(self.call('/api/relations?name=Neha')['grandparent'], ['Raj'])
        self.assertEqual(self.add('New child', priya), 7)

    def test_invalid_input_does_not_mutate(self):
        self.call('/api/child', {'parentId':1, 'name':'No parent'}, 400)
        root = self.add('Raj')
        self.call('/api/root', {'name':'Other root'}, 400)
        for name in ['raj', '', ' ', 'x'*101, 'bad\tname', 'bad\nname', '\x00bad']:
            self.call('/api/child', {'parentId':root, 'name':name}, 400)
        self.call('/api/child', {'parentId':999, 'name':'Absent'}, 400)
        self.call('/api/relations?name=Absent', expected=404)
        self.call('/api/child', {'parentId':root,'name':'Cross origin'},403,trusted=False)
        self.assertEqual(self.call('/api/tree')['count'], 1)

    def test_general_tree_escaping_and_reset(self):
        root = self.add('A "quoted" & <root>')
        for i in range(12): self.add('Child ' + str(i), root)
        self.add('अनुष्का', root)
        report = self.call('/api/relations?id='+str(root))
        self.assertEqual(len(report['children']),13)
        self.assertEqual(report['children'][-1],'अनुष्का')
        self.assertEqual(report['name'],'A "quoted" & <root>')
        self.call('/api/reset',{})
        self.stop(); self.start()
        self.assertEqual(self.call('/api/tree'), {'count':0,'nodes':[]})

    def test_http_and_static_files(self):
        for path, marker in [('/', b'id="tree-art"'),('/style.css',b'new-branch'),('/app.js',b'/api/child')]:
            with urlopen(self.base+path) as r:
                self.assertEqual(r.status,200)
                self.assertIn(marker,r.read())
        self.call('/../family.tsv',expected=404)
        self.call('/api/root', {'name':'Server-owned'})
        body=b'name=Split+request&parentId=1'
        header=(f'POST /api/child HTTP/1.1\r\nHost: 127.0.0.1:{self.port}\r\nX-FamGraph: 1\r\nContent-Length: {len(body)}\r\n\r\n').encode()
        with socket.create_connection(('127.0.0.1',self.port)) as s:
            s.sendall(header+body[:4]); time.sleep(.02); s.sendall(body[4:])
            response=b''
            while True:
                part=s.recv(4096)
                if not part: break
                response+=part
            self.assertIn(b'200 OK', response)
        self.assertEqual(self.call('/api/tree')['count'],2)

    def test_failed_save_rolls_back(self):
        self.stop()
        self.data = Path(self.temp.name) / 'missing-folder' / 'data.tsv'
        self.start()
        self.call('/api/root',{'name':'Cannot save'},500)
        self.assertEqual(self.call('/api/tree')['count'],0)

if __name__ == '__main__':
    unittest.main(verbosity=2)
