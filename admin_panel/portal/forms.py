from django import forms

DB_TYPES = [
    ('postgres', 'PostgreSQL'),
    ('mysql', 'MySQL'),
    ('mongo', 'MongoDB'),
    ('redis', 'Redis'),
]

BE_TYPES = [
    ('node', 'Node.js'),
    ('python', 'Python'),
    ('go', 'Go'),
    ('java', 'Java'),
    ('php', 'PHP'),
    ('static', 'Static only'),
]

FE_BUILDS = [
    ('npm', 'npm run build'),
    ('vite', 'Vite'),
    ('next', 'Next.js'),
    ('hugo', 'Hugo'),
    ('none', 'None (bare HTML)'),
]


class SiteStep1Form(forms.Form):
    domain = forms.CharField(max_length=255, label='Domain name')
    user_owner = forms.CharField(max_length=255, label='Owner username', required=False)
    fe_folder = forms.CharField(max_length=1024, label='FE source folder path')
    be_folder = forms.CharField(max_length=1024, label='BE source folder path')
    be_type = forms.ChoiceField(choices=BE_TYPES, label='Backend type')
    run_cmd = forms.CharField(max_length=1024, label='Run command (inside container)', required=False)
    be_port = forms.IntegerField(initial=3000, label='Backend port')
    fe_build = forms.ChoiceField(choices=FE_BUILDS, label='FE build system')
    db_count = forms.IntegerField(initial=1, min_value=0, max_value=10, label='Number of databases')


class DbCredentialForm(forms.Form):
    db_alias = forms.CharField(max_length=255, label='DB alias (logical name)')
    db_type = forms.ChoiceField(choices=DB_TYPES, label='DB type')
    db_name = forms.CharField(max_length=255, label='Database name')
    db_user = forms.CharField(max_length=255, label='DB username')
    db_password = forms.CharField(
        max_length=255,
        widget=forms.PasswordInput,
        label='DB password',
    )
