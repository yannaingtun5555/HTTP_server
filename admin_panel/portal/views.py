import requests as http_requests
from django.conf import settings
from django.db import DatabaseError, OperationalError
from django.http import JsonResponse
from django.shortcuts import get_object_or_404, redirect, render
from django.views.decorators.csrf import csrf_exempt

from .forms import DbCredentialForm, SiteStep1Form
from .models import BeContainer, DbContainer, Domain, Page

INTERNAL_API_URL = settings.INTERNAL_API_URL
INTERNAL_API_SECRET = settings.INTERNAL_API_SECRET


def dashboard(request):
    """Show all domains ordered by most recently created."""
    try:
        domains = list(Domain.objects.order_by('-created_at'))
        db_error = None
    except (DatabaseError, OperationalError) as exc:
        domains = []
        db_error = (
            'The admin database is not available yet. '
            'The panel is running, but site data cannot be loaded.'
        )
        if settings.DEBUG:
            db_error = f'{db_error} ({exc})'

    return render(request, 'portal/dashboard.html', {
        'domains': domains,
        'db_error': db_error,
    })


def home(request):
    """Simple landing page used to verify the Django container is healthy."""
    return render(request, 'portal/home.html')


def add_site_step1(request):
    """Step 1: collect site/domain info and number of databases."""
    if request.method == 'POST':
        form = SiteStep1Form(request.POST)
        if form.is_valid():
            data = form.cleaned_data
            request.session['site_step1'] = data
            request.session['db_credentials'] = []
            db_count = data.get('db_count', 0)
            if db_count > 0:
                return redirect('add_site_step2', db_index=0)
            else:
                return redirect('deploy_site')
    else:
        form = SiteStep1Form()
    return render(request, 'portal/add_site_step1.html', {'form': form})


def add_site_step2(request, db_index):
    """Step 2 (repeated): collect credentials for each database."""
    step1 = request.session.get('site_step1')
    if not step1:
        return redirect('add_site_step1')

    db_count = step1.get('db_count', 0)
    if db_index >= db_count:
        return redirect('deploy_site')

    if request.method == 'POST':
        form = DbCredentialForm(request.POST)
        if form.is_valid():
            creds = request.session.get('db_credentials', [])
            creds.append(form.cleaned_data)
            request.session['db_credentials'] = creds
            next_index = db_index + 1
            if next_index < db_count:
                return redirect('add_site_step2', db_index=next_index)
            else:
                return redirect('deploy_site')
    else:
        form = DbCredentialForm()

    return render(request, 'portal/add_site_step2.html', {
        'form': form,
        'db_index': db_index,
        'db_number': db_index + 1,
        'db_count': db_count,
    })


def deploy_site(request):
    """Send collected session data to the internal API /deploy endpoint."""
    step1 = request.session.get('site_step1')
    if not step1:
        return redirect('add_site_step1')

    db_credentials = request.session.get('db_credentials', [])

    payload = {
        'domain': step1.get('domain'),
        'user_owner': step1.get('user_owner', ''),
        'fe_folder': step1.get('fe_folder', ''),
        'be_folder': step1.get('be_folder', '') or '',
        'be_type': step1.get('be_type', ''),
        'run_cmd': step1.get('run_cmd', ''),
        'be_port': step1.get('be_port') or 0,
        'fe_build': step1.get('fe_build', ''),
        'dbs': db_credentials,
    }

    result = {'success': False, 'error': '', 'data': {}}

    try:
        response = http_requests.post(
            INTERNAL_API_URL + '/deploy',
            json=payload,
            headers={'X-Internal-Secret': INTERNAL_API_SECRET},
            timeout=60,
        )
        if response.status_code == 200:
            result['success'] = True
            try:
                result['data'] = response.json()
            except ValueError:
                result['data'] = {}
                result['error'] = 'Deployment succeeded, but the API returned non-JSON output.'
        else:
            result['error'] = (
                f'API returned {response.status_code}: {response.text[:500]}'
            )
    except http_requests.exceptions.RequestException as exc:
        result['error'] = str(exc)

    # Clear session data after deploy attempt
    request.session.pop('site_step1', None)
    request.session.pop('db_credentials', None)

    return render(request, 'portal/deploy_result.html', {'result': result})


def site_detail(request, domain):
    """Show detailed information for a single domain."""
    site = get_object_or_404(Domain, domain=domain)
    db_containers = site.db_containers.all().order_by('id')
    be_container = site.be_containers.order_by('-id').first()
    pages = site.pages.all().order_by('path')
    return render(request, 'portal/site_detail.html', {
        'site': site,
        'db_containers': db_containers,
        'be_container': be_container,
        'pages': pages,
    })


def delete_site(request, domain):
    """GET: confirm deletion page. POST: call DELETE on internal API."""
    site = get_object_or_404(Domain, domain=domain)

    if request.method == 'POST':
        result = {'success': False, 'error': ''}
        try:
            response = http_requests.delete(
                INTERNAL_API_URL + '/delete/' + domain,
                headers={'X-Internal-Secret': INTERNAL_API_SECRET},
                timeout=30,
            )
            if response.status_code in (200, 204):
                result['success'] = True
            else:
                result['error'] = (
                    f'API returned {response.status_code}: {response.text[:500]}'
                )
        except http_requests.exceptions.RequestException as exc:
            result['error'] = str(exc)

        return render(request, 'portal/delete_result.html', {
            'site': site,
            'result': result,
        })

    return render(request, 'portal/confirm_delete.html', {'site': site})


def site_status(request, domain):
    """Return JSON with current status of a domain, its DBs and BE container."""
    site = get_object_or_404(Domain, domain=domain)
    db_containers = site.db_containers.all().order_by('id')
    be_container = site.be_containers.order_by('-id').first()
    pages = site.pages.all().order_by('path')

    dbs = [
        {
            'id': db.id,
            'alias': db.db_alias,
            'type': db.db_type,
            'status': db.status,
            'error_msg': db.error_msg,
        }
        for db in db_containers
    ]

    be_info = None
    if be_container:
        be_info = {
            'id': be_container.id,
            'status': be_container.status,
            'be_host': be_container.be_host,
            'be_port': be_container.be_port,
            'error_log': be_container.error_log,
        }

    return JsonResponse({
        'domain': site.domain,
        'status': site.status,
        'error_msg': site.error_msg,
        'dbs': dbs,
        'be': be_info,
        'pages': [
            {
                'id': p.id,
                'path': p.path,
                'content_hash': p.content_hash,
                'size_bytes': p.size_bytes,
            }
            for p in pages
        ],
    })


def site_logs(request, domain):
    """Return JSON with real-time K8s pod logs for a domain."""
    result = {'success': False, 'logs': ''}
    try:
        response = http_requests.get(
            INTERNAL_API_URL + '/logs/' + domain,
            headers={'X-Internal-Secret': INTERNAL_API_SECRET},
            timeout=10,
        )
        if response.status_code == 200:
            data = response.json()
            result['success'] = True
            result['logs'] = data.get('logs', '')
        else:
            result['logs'] = f'Error fetching logs ({response.status_code})'
    except http_requests.exceptions.RequestException as exc:
        result['logs'] = f'Connection error: {exc}'

    return JsonResponse(result)


@csrf_exempt
def github_webhook(request, domain):
    """Receive push events from GitHub/GitLab webhooks to trigger auto-deploy."""
    if request.method != 'POST':
        return JsonResponse({'error': 'POST required'}, status=405)

    event = request.headers.get('X-GitHub-Event', request.headers.get('X-Gitlab-Event', 'push'))

    try:
        response = http_requests.post(
            INTERNAL_API_URL + '/reload',
            headers={'X-Internal-Secret': INTERNAL_API_SECRET},
            timeout=10,
        )
        return JsonResponse({
            'success': True,
            'message': f'Auto-deploy triggered for {domain} (event: {event})',
            'api_status': response.status_code
        })
    except http_requests.exceptions.RequestException as exc:
        return JsonResponse({'success': False, 'error': str(exc)}, status=500)
