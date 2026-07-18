from django.db import models


class Domain(models.Model):
    domain = models.CharField(max_length=255, unique=True)
    user_owner = models.CharField(max_length=255, blank=True)
    fe_folder = models.CharField(max_length=1024, blank=True)
    be_folder = models.CharField(max_length=1024, blank=True)
    be_type = models.CharField(max_length=64, blank=True)
    run_cmd = models.CharField(max_length=1024, blank=True)
    be_port = models.IntegerField(default=3000)
    fe_build = models.CharField(max_length=64, blank=True)
    db_count = models.IntegerField(default=0)
    status = models.CharField(max_length=64, default='pending')
    error_msg = models.TextField(blank=True)
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        managed = False
        db_table = 'domains'

    def __str__(self):
        return self.domain


class DbContainer(models.Model):
    domain = models.ForeignKey(
        Domain,
        on_delete=models.CASCADE,
        db_column='domain_id',
        related_name='db_containers',
    )
    db_alias = models.CharField(max_length=255)
    db_type = models.CharField(max_length=64)
    db_name = models.CharField(max_length=255)
    k8s_namespace = models.CharField(max_length=255, blank=True)
    k8s_deployment = models.CharField(max_length=255, blank=True)
    k8s_service = models.CharField(max_length=255, blank=True)
    cluster_ip = models.CharField(max_length=128, blank=True)
    port = models.IntegerField(null=True, blank=True)
    connection_str = models.TextField(blank=True)
    status = models.CharField(max_length=64, default='pending')
    error_msg = models.TextField(blank=True)
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        managed = False
        db_table = 'db_containers'

    def __str__(self):
        return f'{self.domain.domain} / {self.db_alias}'


class BeContainer(models.Model):
    domain = models.ForeignKey(
        Domain,
        on_delete=models.CASCADE,
        db_column='domain_id',
        related_name='be_containers',
    )
    image_tag = models.CharField(max_length=512, blank=True)
    k8s_namespace = models.CharField(max_length=255, blank=True)
    k8s_deployment = models.CharField(max_length=255, blank=True)
    k8s_service = models.CharField(max_length=255, blank=True)
    be_host = models.CharField(max_length=255, blank=True)
    be_port = models.IntegerField(null=True, blank=True)
    status = models.CharField(max_length=64, default='pending')
    error_log = models.TextField(blank=True)
    deployed_at = models.DateTimeField(null=True, blank=True)

    class Meta:
        managed = False
        db_table = 'be_containers'

    def __str__(self):
        return f'{self.domain.domain} / be'


class Page(models.Model):
    domain = models.ForeignKey(
        Domain,
        on_delete=models.CASCADE,
        db_column='domain_id',
        related_name='pages',
    )
    path = models.CharField(max_length=1024)
    content_hash = models.CharField(max_length=255, blank=True)
    size_bytes = models.BigIntegerField(null=True, blank=True)
    built_at = models.DateTimeField(null=True, blank=True)

    class Meta:
        managed = False
        db_table = 'pages'

    def __str__(self):
        return f'{self.domain.domain} {self.path}'
