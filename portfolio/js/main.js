/* ==========================================================================
   Antigravity Main Application Script
   Dynamic Resolution Adjuster, Parallax & Spotlight Engine
   ========================================================================== */

document.addEventListener('DOMContentLoaded', () => {

  // 1. Dynamic Resolution & Viewport DPI Adjuster
  (function initDynamicResolution() {
    function adjustResolution() {
      const vh = window.innerHeight * 0.01;
      document.documentElement.style.setProperty('--vh', `${vh}px`);

      const dpr = window.devicePixelRatio || 1;
      document.documentElement.style.setProperty('--dpr', `${dpr}`);
    }

    window.addEventListener('resize', adjustResolution);
    adjustResolution();
  })();

  // 2. Smooth Navigation Anchor Scrolling with Dynamic Header Offset
  (function initSmoothScroll() {
    const anchors = document.querySelectorAll('a[href^="#"]');
    anchors.forEach(anchor => {
      anchor.addEventListener('click', function (e) {
        const targetId = this.getAttribute('href');
        if (!targetId || targetId === '#') return;

        const targetEl = document.querySelector(targetId);
        if (targetEl) {
          e.preventDefault();
          const headerOffset = 80;
          const elementPosition = targetEl.getBoundingClientRect().top;
          const offsetPosition = elementPosition + window.pageYOffset - headerOffset;

          window.scrollTo({
            top: offsetPosition,
            behavior: 'smooth'
          });
        }
      });
    });
  })();

  // 3. Full Page Scroll-Driven Parallax & Blur Background Controller
  (function initScrollBackground() {
    let bgWrapper = document.querySelector('.global-bg-wrapper');
    if (!bgWrapper) {
      bgWrapper = document.createElement('div');
      bgWrapper.className = 'global-bg-wrapper';
      bgWrapper.innerHTML = `<img src="assets/hero_bg.jpg" alt="Systems Architecture Background" class="global-bg-image" id="globalBgImg" />`;
      document.body.prepend(bgWrapper);
    }

    const bgImg = document.getElementById('globalBgImg');
    if (!bgImg) return;

    let ticking = false;

    function onScroll() {
      const scrollY = window.pageYOffset || document.documentElement.scrollTop;
      const maxScroll = (document.documentElement.scrollHeight - window.innerHeight) || 1;
      const scrollFraction = Math.min(Math.max(scrollY / maxScroll, 0), 1);

      // Dynamic scale (1.0 to 1.18), translateY (0 to 120px), opacity (0.24 to 0.12), and blur (0 to 6px)
      const scale = 1.0 + scrollFraction * 0.18;
      const translateY = scrollFraction * 120;
      const opacity = 0.24 - scrollFraction * 0.12;
      const blur = scrollFraction * 6;

      bgImg.style.transform = `scale(${scale}) translateY(${translateY}px)`;
      bgImg.style.opacity = opacity.toFixed(3);
      bgImg.style.filter = `saturate(1.2) contrast(1.1) blur(${blur.toFixed(1)}px)`;

      ticking = false;
    }

    window.addEventListener('scroll', () => {
      if (!ticking) {
        window.requestAnimationFrame(onScroll);
        ticking = true;
      }
    }, { passive: true });

    onScroll();
  })();

  // 4. Interactive Background Canvas Particle Mesh with Resolution Adaptation
  (function initCanvasMesh() {
    if (document.getElementById('bgCanvas')) return;

    const canvas = document.createElement('canvas');
    canvas.id = 'bgCanvas';
    document.body.prepend(canvas);

    const ctx = canvas.getContext('2d');
    let width = (canvas.width = window.innerWidth);
    let height = (canvas.height = window.innerHeight);

    let particles = [];
    const particleCount = Math.min(Math.floor(window.innerWidth / 18), 75);
    const mouse = { x: null, y: null, radius: 200 };

    window.addEventListener('mousemove', (e) => {
      mouse.x = e.clientX;
      mouse.y = e.clientY;
    });

    window.addEventListener('resize', () => {
      width = canvas.width = window.innerWidth;
      height = canvas.height = window.innerHeight;
    });

    class Particle {
      constructor() {
        this.x = Math.random() * width;
        this.y = Math.random() * height;
        this.vx = (Math.random() - 0.5) * 0.45;
        this.vy = (Math.random() - 0.5) * 0.45;
        this.radius = Math.random() * 1.5 + 1;
        this.alpha = Math.random() * 0.5 + 0.2;
      }

      update() {
        this.x += this.vx;
        this.y += this.vy;

        if (this.x < 0 || this.x > width) this.vx *= -1;
        if (this.y < 0 || this.y > height) this.vy *= -1;

        if (mouse.x && mouse.y) {
          const dx = mouse.x - this.x;
          const dy = mouse.y - this.y;
          const dist = Math.sqrt(dx * dx + dy * dy);
          if (dist < mouse.radius) {
            const angle = Math.atan2(dy, dx);
            const force = (mouse.radius - dist) / mouse.radius;
            this.x -= Math.cos(angle) * force * 1.6;
            this.y -= Math.sin(angle) * force * 1.6;
          }
        }
      }

      draw() {
        ctx.beginPath();
        ctx.arc(this.x, this.y, this.radius, 0, Math.PI * 2);
        ctx.fillStyle = `rgba(56, 189, 248, ${this.alpha})`;
        ctx.fill();
      }
    }

    for (let i = 0; i < particleCount; i++) {
      particles.push(new Particle());
    }

    function animate() {
      ctx.clearRect(0, 0, width, height);

      for (let i = 0; i < particles.length; i++) {
        particles[i].update();
        particles[i].draw();

        for (let j = i + 1; j < particles.length; j++) {
          const dx = particles[i].x - particles[j].x;
          const dy = particles[i].y - particles[j].y;
          const dist = Math.sqrt(dx * dx + dy * dy);

          if (dist < 130) {
            ctx.beginPath();
            ctx.moveTo(particles[i].x, particles[i].y);
            ctx.lineTo(particles[j].x, particles[j].y);
            ctx.strokeStyle = `rgba(56, 189, 248, ${0.16 * (1 - dist / 130)})`;
            ctx.lineWidth = 0.6;
            ctx.stroke();
          }
        }
      }

      requestAnimationFrame(animate);
    }

    animate();
  })();

  // 5. Mouse Spotlight Tracking on All Engineering Cards
  const spotlightCards = document.querySelectorAll('.metric-card, .philosophy-card, .ironagent-card, .feature-box, .pipeline-step, .project-card, .skill-category-card, .timeline-card');
  spotlightCards.forEach(card => {
    card.addEventListener('mousemove', (e) => {
      const rect = card.getBoundingClientRect();
      const x = e.clientX - rect.left;
      const y = e.clientY - rect.top;
      card.style.setProperty('--mouse-x', `${x}px`);
      card.style.setProperty('--mouse-y', `${y}px`);
    });
  });

  // 6. Scroll-Reveal Intersection Observer
  (function initScrollReveal() {
    const revealElements = document.querySelectorAll('section, .metric-card, .philosophy-card, .feature-box, .project-card, .pipeline-step, .timeline-card');
    revealElements.forEach(el => el.classList.add('reveal-on-scroll'));

    const observer = new IntersectionObserver((entries) => {
      entries.forEach(entry => {
        if (entry.isIntersecting) {
          entry.target.classList.add('reveal-visible');
        }
      });
    }, { threshold: 0.08, rootMargin: '0px 0px -40px 0px' });

    revealElements.forEach(el => observer.observe(el));
  })();

  // 7. Mobile Navigation Toggle & Auto-Close
  const mobileToggle = document.getElementById('mobileToggle');
  const navLinks = document.getElementById('navLinks');

  if (mobileToggle && navLinks) {
    mobileToggle.addEventListener('click', () => {
      navLinks.classList.toggle('active');
    });

    navLinks.querySelectorAll('a').forEach(link => {
      link.addEventListener('click', () => {
        navLinks.classList.remove('active');
      });
    });
  }

  // 8. Project Category Filtering
  const filterBtns = document.querySelectorAll('.filter-btn');
  const projectCards = document.querySelectorAll('.project-card');

  filterBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      filterBtns.forEach(b => b.classList.remove('active'));
      btn.classList.add('active');

      const filter = btn.getAttribute('data-filter');

      projectCards.forEach(card => {
        const categories = card.getAttribute('data-category');
        if (filter === 'all' || (categories && categories.includes(filter))) {
          card.style.display = 'flex';
          card.style.opacity = '1';
        } else {
          card.style.display = 'none';
          card.style.opacity = '0';
        }
      });
    });
  });

  // 9. Active Navbar Highlight Tracking & Scroll Elevation
  const sections = document.querySelectorAll('section[id]');
  const navItems = document.querySelectorAll('.nav-links a');
  const navbar = document.getElementById('navbar');

  window.addEventListener('scroll', () => {
    let current = '';
    const scrollY = window.pageYOffset;

    if (navbar) {
      if (scrollY > 30) {
        navbar.classList.add('scrolled');
      } else {
        navbar.classList.remove('scrolled');
      }
    }

    sections.forEach(section => {
      const sectionHeight = section.offsetHeight;
      const sectionTop = section.offsetTop - 110;
      if (scrollY > sectionTop && scrollY <= sectionTop + sectionHeight) {
        current = section.getAttribute('id');
      }
    });

    navItems.forEach(item => {
      item.classList.remove('active');
      if (item.getAttribute('href') === `#${current}`) {
        item.classList.add('active');
      }
    });
  }, { passive: true });
});
