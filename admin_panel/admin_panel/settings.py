import os
import urllib.parse
from pathlib import Path

# Build paths inside the project like this: BASE_DIR / 'subdir'.
BASE_DIR = Path(__file__).resolve().parent.parent

# SECURITY WARNING: keep the secret key used in production secret!
SECRET_KEY = os.environ.get('DJANGO_SECRET_KEY', 'dev-secret-change-me')

# SECURITY WARNING: don't run with debug turned on in production!
DEBUG = os.environ.get('DEBUG', 'True') == 'True'

ALLOWED_HOSTS = ['*']

# Application definition
INSTALLED_APPS = [
    'django.contrib.contenttypes',
    'django.contrib.sessions',
    'django.contrib.staticfiles',
    'portal',
]

MIDDLEWARE = [
    'django.middleware.security.SecurityMiddleware',
    'django.contrib.sessions.middleware.SessionMiddleware',
    'django.middleware.common.CommonMiddleware',
    'django.middleware.csrf.CsrfViewMiddleware',
    'django.middleware.clickjacking.XFrameOptionsMiddleware',
]

ROOT_URLCONF = 'admin_panel.urls'

TEMPLATES = [
    {
        'BACKEND': 'django.template.backends.django.DjangoTemplates',
        'DIRS': [BASE_DIR / 'templates'],
        'APP_DIRS': False,
        'OPTIONS': {
            'loaders': [
                'django.template.loaders.filesystem.Loader',
                'django.template.loaders.app_directories.Loader',
            ],
            'context_processors': [
                'django.template.context_processors.debug',
                'django.template.context_processors.request',
            ],
        },
    },
]

WSGI_APPLICATION = 'admin_panel.wsgi.application'

# Session
SESSION_ENGINE = 'django.contrib.sessions.backends.signed_cookies'

# Database
# Parse DATABASE_URL if present, otherwise use defaults
_db_url = os.environ.get('DATABASE_URL', '')
if _db_url:
    _parsed = urllib.parse.urlparse(_db_url)
    DATABASES = {
        'default': {
            'ENGINE': 'django.db.backends.postgresql',
            'NAME': _parsed.path.lstrip('/'),
            'USER': _parsed.username or 'admin',
            'PASSWORD': _parsed.password or 'changeme',
            'HOST': _parsed.hostname or '127.0.0.1',
            'PORT': str(_parsed.port or 5432),
        }
    }
else:
    DATABASES = {
        'default': {
            'ENGINE': 'django.db.backends.postgresql',
            'NAME': 'server_db',
            'USER': 'admin',
            'PASSWORD': 'changeme',
            'HOST': '127.0.0.1',
            'PORT': '5432',
        }
    }

# Static files
STATIC_URL = '/static/'
STATIC_ROOT = BASE_DIR / 'staticfiles'

# Internationalization
LANGUAGE_CODE = 'en-us'
TIME_ZONE = 'UTC'
USE_I18N = True
USE_TZ = True

# Internal API settings
INTERNAL_API_URL = os.environ.get('INTERNAL_API_URL', 'http://127.0.0.1:8000/_api/internal')
INTERNAL_API_SECRET = os.environ.get('INTERNAL_API_SECRET', 'changeme-secret')

# Default primary key field type
DEFAULT_AUTO_FIELD = 'django.db.models.BigAutoField'
