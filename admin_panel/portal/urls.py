from django.urls import path
from . import views

urlpatterns = [
    path('', views.dashboard, name='dashboard'),
    path('add/', views.add_site_step1, name='add_site_step1'),
    path('add/db/<int:db_index>/', views.add_site_step2, name='add_site_step2'),
    path('deploy/', views.deploy_site, name='deploy_site'),
    path('site/<str:domain>/', views.site_detail, name='site_detail'),
    path('site/<str:domain>/delete/', views.delete_site, name='delete_site'),
    path('site/<str:domain>/status.json', views.site_status, name='site_status'),
]
