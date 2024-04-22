import os
import unittest
import subprocess
import sys
import psutil
import platform


class LavaSH(unittest.TestCase):
    res_score = 0

    def _run(self, command, expected_out='', expected_err='', code=0, pre=None, post=None):
        if pre:
            pre(self)
        processes = psutil.pids()
        p = subprocess.Popen(['./lavash', '-c', command], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = p.communicate()
        self.assertEqual(code, p.returncode)
        self.assertEqual(out.decode(), expected_out)
        self.assertEqual(err.decode(), expected_err)
        self.assertFalse(set(psutil.pids()) - set(processes), "Some processes still exist. "
                                                              "This may False positive on local runs")
        if post:
            post(self)

    @classmethod
    def tearDownClass(cls):
        print()
        print('Score:', cls.res_score)
        with open('res.txt', 'w') as f:
            f.write(str(cls.res_score))

    @staticmethod
    def _remove_file(name):
        def inner(self):
            if os.path.exists(name):
                os.remove(name)
        return inner

    @staticmethod
    def _create_file(name, data):
        def inner(self):
            if os.path.exists(name):
                os.remove(name)
            with open(name, 'w') as f:
                f.write(data)
        return inner

    @staticmethod
    def _seq(*acts):
        def inner(self):
            for act in acts:
                act(self)
        return inner

    @staticmethod
    def _check_file_contains(name, expected):
        def inner(self):
            if expected is None:
                self.assertFalse(os.path.exists(name))
            else:
                with open(name, 'r') as f:
                    self.assertEqual(f.read(), expected)
        return inner

    @staticmethod
    def score(score):
        def decorator(func):
            def inner(self, *args, **kwargs):
                try:
                    res = func(self, *args, **kwargs)
                except Exception as e:
                    raise
                else:
                    LavaSH.res_score += score
                    return res
            return inner
        return decorator

    @score(10)
    def test_simple(self):
        self._run('echo', '\n')
        self._run('echo hello', 'hello\n')
        self._run('echo hello world', 'hello world\n')

    @score(10)
    def test_relative_path(self):
        self._run('./tools/print_args', './tools/print_args\n')
        self._run('./tools/print_args 1', './tools/print_args\n1\n')
        self._run('./tools/print_args 1 2 3 4', './tools/print_args\n1\n2\n3\n4\n')

    @score(20)
    def test_spaces(self):
        self._run('./tools/print_args 1 "2 3" 4', './tools/print_args\n1\n2 3\n4\n')
        self._run('./tools/print_args " \' " "1 2 3 4\\\\"', './tools/print_args\n \' \n1 2 3 4\\\n')

    @score(20)
    def test_pipe(self):
        if platform.system() == 'Darwin':
            self._run('echo hello | wc', '       1       1       6\n')
            self._run('echo hello | wc | wc', '       1       3      25\n')
            self._run('echo hello | wc | wc | wc | wc | wc | wc | wc | wc | wc | wc | wc | wc | wc | wc | wc | wc | wc',
                      '       1       3      25\n')
        else:
            self._run('echo hello | wc', '      1       1       6\n')
            self._run('echo hello | wc | wc', '      1       3      24\n')
            self._run('echo hello | wc | wc | wc | wc | wc | wc | wc | wc | wc | wc | wc | wc | wc | wc | wc | wc | wc',
                      '      1       3      24\n')

    @score(10)
    def test_out_files(self):
        self._run('echo hello > output.txt',
                  pre=self._remove_file('output.txt'),
                  post=self._check_file_contains('output.txt', 'hello\n'))
        self._run('echo hello >output.txt',
                  pre=self._remove_file('output.txt'),
                  post=self._check_file_contains('output.txt', 'hello\n'))

    @score(10)
    def test_in_files(self):
        if platform.system() == 'Darwin':
            self._run('wc < input.txt',
                      '       9       9      26\n',
                      pre=self._create_file('input.txt', 'abracabra\n1\n2\n3\n4\n5\n6\n7\n8\n'))
        else:
            self._run('wc < input.txt',
                      ' 9  9 26\n',
                      pre=self._create_file('input.txt', 'abracabra\n1\n2\n3\n4\n5\n6\n7\n8\n'))

    @score(10)
    def test_in_out_files(self):
        self._run('cat > output.txt < input.txt',
                  pre=self._seq(self._create_file('input.txt', '1\n2\n3\n'), self._remove_file('output.txt')),
                  post=self._check_file_contains('output.txt', '1\n2\n3\n'))

    @score(10)
    def test_pipe_and_files(self):
        if platform.system() == 'Darwin':
            self._run('echo hello1 > output.txt | wc',
                      '       0       0       0\n',
                      pre=self._remove_file('output.txt'),
                      post=self._check_file_contains('output.txt', 'hello1\n'))
            self._run('echo hello1 | wc < input.txt',
                      '       1       1       4\n',
                      pre=self._create_file('input.txt', '123\n'))
        else:
            self._run('echo hello1 > output.txt | wc',
                      '      0       0       0\n',
                      pre=self._remove_file('output.txt'),
                      post=self._check_file_contains('output.txt', 'hello1\n'))
            self._run('echo hello1 | wc < input.txt',
                      '1 1 4\n',
                      pre=self._create_file('input.txt', '123\n'))

    @score(10)
    def test_escaping_in_redirect(self):
        self._run('echo hello >\\\".txt',
                  pre=self._remove_file('\".txt'),
                  post=self._check_file_contains('\".txt', 'hello\n'))

    @score(10)
    def test_yoda(self):
        self._run('>1.txt echo hello',
                  pre=self._remove_file('1.txt'),
                  post=self._check_file_contains('1.txt', 'hello\n'))
        self._run('< 1.txt cat', 'hello',
                  pre=self._create_file('1.txt', 'hello'))

    @score(10)
    def test_escaped_signs_do_not_redirect(self):
        self._run('echo hello \">\" 1.txt', 'hello > 1.txt\n',
                  pre=self._remove_file('1.txt'),
                  post=self._check_file_contains('1.txt', None))

    @score(10)
    def test_1984(self):
        # Этот тест не совместим с bash
        self._run('echo hello | 1984', '')

    @score(10)
    def test_return_code_simple(self):
        self._run('true', code=0)
        self._run('false', code=1)

    @score(20)
    def test_line_or(self):
        self._run('echo hello || echo world', 'hello\n')
        self._run('false || echo world', 'world\n')
        self._run('echo < unexisting.txt || echo world', 'world\n',
                  expected_err='./lavash: line 1: unexisting.txt: No such file or directory\n')
        self._run('true || echo world', '')
        self._run('false || n',
                  code=127,
                  expected_err='./lavash: line 1: n: command not found\n')

    @score(20)
    def test_line_and(self):
        self._run('echo hello && echo world', 'hello\nworld\n')
        self._run('true && echo world', 'world\n')
        self._run('false && echo world', '',
                  code=1)
        self._run('< unexisting.txt && echo world',
                  code=1,
                  expected_err='./lavash: line 1: unexisting.txt: No such file or directory\n')
        self._run('false && n',
                  code=1)

    @score(20)
    def test_line_and_or(self):
        self._run('echo hello && echo world1 || echo world2', 'hello\nworld1\n')
        self._run('< unexisting.txt && echo 1 || echo 2', '2\n',
                  expected_err='./lavash: line 1: unexisting.txt: No such file or directory\n')

        self._run('echo 1 > 1.txt && echo 2 < unexisting.txt > 2.txt && echo 3 > 3.txt || echo 4 > 4.txt',
                  pre=lambda self: self._remove_file('1.txt') and self._remove_file('2.txt') and self._remove_file('3.txt') and self._remove_file('4.txt'),
                  post=lambda self: self._check_file_contains('1.txt', '1\n') and self._check_file_contains('2.txt', '2\n') and self._check_file_contains('4.txt', '4\n'),
                  expected_err='./lavash: line 1: unexisting.txt: No such file or directory\n')
        self._run('er && echo 1 | cat || echo 2', '2\n',
                  expected_err='./lavash: line 1: er: command not found\n')

    @score(20)
    def test_line_and_or_2(self):
        if platform.system() == 'Darwin':
            self._run('false || true && false || false', '',
                      code=1)
            self._run('true || false && true', '')
            self._run('er | wc || echo 1', '       0       0       0\n',
                      expected_err='./lavash: line 1: er: command not found\n')
            self._run('echo 2 | wc || echo 1', '       1       1       2\n')
            self._run('echo 2 || echo 1 | wc', '2\n')
            self._run('er || echo 1 | wc', '       1       1       2\n',
                      expected_err='./lavash: line 1: er: command not found\n')
        else:
            self._run('false || true && false || false', '',
                      code=1)
            self._run('true || false && true', '')
            self._run('er | wc || echo 1', '      0       0       0\n',
                      expected_err='./lavash: line 1: er: command not found\n')
            self._run('echo 2 | wc || echo 1', '      1       1       2\n')
            self._run('echo 2 || echo 1 | wc', '2\n')
            self._run('er || echo 1 | wc', '      1       1       2\n',
                      expected_err='./lavash: line 1: er: command not found\n')


if __name__ == '__main__':
    test_suite = unittest.defaultTestLoader.discover('.', 'test.py')
    test_runner = unittest.TextTestRunner(resultclass=unittest.TextTestResult)
    result = test_runner.run(test_suite)
    try:
        with open('res.txt', 'r') as f:
            res_score = int(f.read())
    except:
        res_score = 0
    if result.wasSuccessful() or res_score != 0:
        sys.exit(0)
    sys.exit(1)
