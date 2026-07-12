from django.urls import path, include

urlpatterns = [
    path('_admin/', include('portal.urls')),
]
