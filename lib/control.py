from lib.log import log_err

class Control(object):
    def __str__(self):
        return self.__class__.__name__.lower()

    def proc(self, op, args):
        try:
            if op[0] == '_':
                log_err(self, 'invalid operation %s' % op)
                return
            func = getattr(self, op)
            if not func:
                log_err(self, 'invalid operation %s' % op)
                return
            return func(*args)
        except:
            log_err(self, 'failed to process %s' % op)
