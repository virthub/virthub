from defaults import SHOW_DEBUG, SHOW_ERROR

def _get_name(obj):
    return obj.__class__.__name__

def log_get(obj, text):
    return _get_name(obj) + ': ' + str(text)

def log(text):
    if SHOW_DEBUG:
        print(str(text))

def log_err(obj, text):
    if SHOW_ERROR:
        if obj:
            print(log_get(obj, text))
        else:
            log(text)
