from django.urls import include, path

from portal import views as portal_views

urlpatterns = [
    path('', portal_views.home, name='home'),
    path('_admin/', include('portal.urls')),
]
