(function () {
  // Menú móvil
  var btn = document.querySelector('.nav-toggle');
  var nav = document.getElementById('site-nav');
  if (btn && nav) {
    btn.addEventListener('click', function () {
      var open = nav.classList.toggle('open');
      btn.setAttribute('aria-expanded', open ? 'true' : 'false');
    });
  }

  // Formulario de contacto: abre el correo del usuario con el mensaje ya escrito
  var form = document.getElementById('contact-form');
  if (form) {
    form.addEventListener('submit', function (e) {
      e.preventDefault();
      var f = new FormData(form);
      var cuerpo = f.get('mensaje') + '\n\n— ' + f.get('nombre') + ' (' + f.get('correo') + ')';
      var url = 'mailto:escalonesdevida@gmail.com?subject=' +
        encodeURIComponent(f.get('asunto')) + '&body=' + encodeURIComponent(cuerpo);
      var msg = document.getElementById('form-msg');
      if (msg) { msg.textContent = 'Se abrió tu programa de correo con el mensaje listo. Solo falta enviarlo.'; }
      window.location.href = url;
    });
  }
})();
