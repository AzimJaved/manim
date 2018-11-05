from big_ol_pile_of_manim_imports import *
from old_projects.div_curl import PureAirfoilFlow
from old_projects.div_curl import VectorFieldSubmobjectFlow
from old_projects.div_curl import VectorFieldPointFlow
from old_projects.div_curl import four_swirls_function


class CreationDestructionMobject(VMobject):
    CONFIG = {
        "start_time": 0,
        "frequency": 0.25,
        "max_ratio_shown": 0.3,
        "use_copy": True,
    }

    def __init__(self, template, **kwargs):
        VMobject.__init__(self, **kwargs)
        if self.use_copy:
            self.ghost_mob = template.copy().fade(1)
            self.add(self.ghost_mob)
        else:
            self.ghost_mob = template
            # Don't add
        self.shown_mob = template.deepcopy()
        self.shown_mob.clear_updaters()
        self.add(self.shown_mob)
        self.total_time = self.start_time

        def update(mob, dt):
            mob.total_time += dt
            period = 1.0 / mob.frequency
            unsmooth_alpha = (mob.total_time % period) / period
            alpha = bezier([0, 0, 1, 1])(unsmooth_alpha)
            mrs = mob.max_ratio_shown
            mob.shown_mob.pointwise_become_partial(
                mob.ghost_mob,
                max(interpolate(-mrs, 1, alpha), 0),
                min(interpolate(0, 1 + mrs, alpha), 1),
            )

        self.add_updater(update)


class Eddy(VMobject):
    CONFIG = {
        "cd_mob_config": {
            "frequency": 0.2,
            "max_ratio_shown": 0.3
        },
        "n_spirils": 5,
        "n_layers": 20,
        "radius": 1,
        "colors": [BLUE_A, BLUE_E],
    }

    def __init__(self, **kwargs):
        VMobject.__init__(self, **kwargs)
        lines = self.get_lines()
        # self.add(lines)
        self.add(*[
            CreationDestructionMobject(line, **self.cd_mob_config)
            for line in lines
        ])
        self.randomize_times()

    def randomize_times(self):
        for submob in self.submobjects:
            if hasattr(submob, "total_time"):
                T = 1.0 / submob.frequency
                submob.total_time = T * random.random()

    def get_lines(self):
        a = 0.2
        return VGroup(*[
            self.get_line(r=self.radius * (1 - a + 2 * a * random.random()))
            for x in range(self.n_layers)
        ])

    def get_line(self, r):
        return ParametricFunction(
            lambda t: r * (t + 1)**(-1) * np.array([
                np.cos(TAU * t),
                np.sin(TAU * t),
                0,
            ]),
            t_min=0.1 * random.random(),
            t_max=self.n_spirils,
            stroke_width=1,
            color=interpolate_color(*self.colors, random.random())
        )


class Chaos(Eddy):
    CONFIG = {
        "n_lines": 12,
        "height": 1,
        "width": 2,
        "n_midpoints": 4,
        "cd_mob_config": {
            "use_copy": False,
            "frequency": 1,
            "max_ratio_shown": 0.8
        }
    }

    def __init__(self, **kwargs):
        VMobject.__init__(self, **kwargs)
        rect = Rectangle(height=self.height, width=self.width)
        rect.move_to(ORIGIN, DL)
        rect.fade(1)
        self.rect = rect
        self.add(rect)

        lines = self.get_lines()
        self.add(*[
            CreationDestructionMobject(line, **self.cd_mob_config)
            for line in lines
        ])
        self.randomize_times()
        lines.fade(1)
        self.add(lines)

    def get_lines(self):
        return VGroup(*[
            self.get_line(y)
            for y in np.linspace(0, self.height, self.n_lines)
        ])

    def get_line(self, y):
        frequencies = [0] + list(2 + 2 * np.random.random(self.n_midpoints)) + [0]
        rect = self.rect
        line = Line(
            y * UP, y * UP + self.width * RIGHT,
            stroke_width=1
        )
        line.insert_n_anchor_points(self.n_midpoints)
        line.total_time = random.random()
        delta_h = self.height / (self.n_lines - 1)

        def update(line, dt):
            x0, y0 = rect.get_corner(DL)[:2]
            x1, y1 = rect.get_corner(UR)[:2]
            line.total_time += dt
            xs = np.linspace(x0, x1, self.n_midpoints + 2)
            new_anchors = [
                np.array([
                    x + 1.0 * delta_h * np.cos(f * line.total_time),
                    y0 + y + 1.0 * delta_h * np.cos(f * line.total_time),
                    0
                ])
                for (x, f) in zip(xs, frequencies)
            ]
            line.set_points_smoothly(new_anchors)

        line.add_updater(update)
        return line


class DoublePendulum(VMobject):
    CONFIG = {
        "start_angles": [3 * PI / 7, 3 * PI / 4],
        "color1": BLUE,
        "color2": RED,
    }

    def __init__(self, **kwargs):
        VMobject.__init__(self, **kwargs)
        line1 = Line(ORIGIN, UP)
        dot1 = Dot(color=self.color1)
        dot1.add_updater(lambda d: d.move_to(line1.get_end()))
        line2 = Line(UP, 2 * UP)
        dot2 = Dot(color=self.color2)
        dot2.add_updater(lambda d: d.move_to(line2.get_end()))
        self.add(line1, line2, dot1, dot2)

        # Largely copied from https://scipython.com/blog/the-double-pendulum/
        # Pendulum rod lengths (m), bob masses (kg).
        L1, L2 = 1, 1
        m1, m2 = 1, 1
        # The gravitational acceleration (m.s-2).
        g = 9.81

        self.state_vect = np.array([
            self.start_angles[0], 0,
            self.start_angles[1], 0,
        ])
        self.state_vect += np.random.random(4) * 1e-7

        def update(group, dt):
            for x in range(2):
                line1, line2 = group.submobjects[:2]
                theta1, z1, theta2, z2 = group.state_vect

                c, s = np.cos(theta1 - theta2), np.sin(theta1 - theta2)

                theta1dot = z1
                z1dot = (m2 * g * np.sin(theta2) * c - m2 * s * (L1 * (z1**2) * c + L2 * z2**2) -
                         (m1 + m2) * g * np.sin(theta1)) / L1 / (m1 + m2 * s**2)
                theta2dot = z2
                z2dot = ((m1 + m2) * (L1 * (z1**2) * s - g * np.sin(theta2) + g * np.sin(theta1) * c) +
                         m2 * L2 * (z2**2) * s * c) / L2 / (m1 + m2 * s**2)

                group.state_vect += 0.5 * dt * np.array([
                    theta1dot, z1dot, theta2dot, z2dot,
                ])
                group.state_vect[1::2] *= 0.9999

            p1 = L1 * np.sin(theta1) * RIGHT - L1 * np.cos(theta1) * UP
            p2 = p1 + L2 * np.sin(theta2) * RIGHT - L2 * np.cos(theta2) * UP

            line1.put_start_and_end_on(ORIGIN, p1)
            line2.put_start_and_end_on(p1, p2)

        self.add_updater(update)


class DoublePendulums(VGroup):
    def __init__(self, **kwargs):
        colors = [BLUE, RED, YELLOW, PINK, MAROON_B, PURPLE, GREEN]
        VGroup.__init__(
            self,
            *[
                DoublePendulum(
                    color1=random.choice(colors),
                    color2=random.choice(colors),
                )
                for x in range(5)
            ],
            **kwargs,
        )


class Diffusion(VMobject):
    CONFIG = {
        "height": 1.5,
        "n_dots": 1000,
        "colors": [RED, BLUE]
    }

    def __init__(self, **kwargs):
        VMobject.__init__(self, **kwargs)
        self.add_dots()
        self.add_invisible_circles()

    def add_dots(self):
        dots = VGroup(*[Dot() for x in range(self.n_dots)])
        dots.arrange_submobjects_in_grid(buff=SMALL_BUFF)
        dots.center()
        dots.set_height(self.height)
        dots.sort_submobjects(lambda p: p[0])
        dots[:len(dots) // 2].set_color(self.colors[0])
        dots[len(dots) // 2:].set_color(self.colors[1])
        dots.set_fill(opacity=0.8)
        self.dots = dots
        self.add(dots)

    def add_invisible_circles(self):
        circles = VGroup()
        for dot in self.dots:
            point = dot.get_center()
            radius = get_norm(point)
            circle = Circle(radius=radius)
            circle.rotate(angle_of_vector(point))
            circle.fade(1)
            circles.add(circle)
            self.add_updater_to_dot(dot, circle)
        self.add(circles)

    def add_updater_to_dot(self, dot, circle):
        dot.total_time = 0
        radius = get_norm(dot.get_center())
        freq = 0.1 + 0.05 * random.random() + 0.05 / radius

        def update(dot, dt):
            dot.total_time += dt
            prop = (freq * dot.total_time) % 1
            dot.move_to(circle.point_from_proportion(prop))

        dot.add_updater(update)


class NavierStokesEquations(TexMobject):
    CONFIG = {
        "tex_to_color_map": {
            "\\rho": YELLOW,
            "\\mu": RED,
            "\\textbf{u}": BLUE,
        },
        "width": 10,
    }

    def __init__(self, **kwargs):
        u_tex = "\\textbf{u}"
        TexMobject.__init__(
            self,
            "\\rho",
            "\\left("
            "{\\partial", u_tex, "\\over",
            "\\partial", "t}",
            "+",
            u_tex, "\\cdot", "\\nabla", u_tex,
            "\\right)",
            "=",
            "-", "\\nabla", "p", "+",
            "\\mu", "\\nabla^2", u_tex, "+",
            "\\frac{1}{3}", "\\mu", "\\nabla",
            "(", "\\nabla", "\\cdot", u_tex, ")", "+",
            "\\textbf{F}",
            **kwargs
        )
        self.set_width(self.width)


class Test(Scene):
    def construct(self):
        self.add(DoublePendulums())
        self.wait(30)

# Scenes


class EddyReference(Scene):
    CONFIG = {
        "radius": 0.5,
        "label": "Eddy",
        "label": "",
    }

    def construct(self):
        eddy = Eddy()
        new_eddy = eddy.get_lines()
        label = TextMobject(self.label)
        label.next_to(new_eddy, UP)

        self.play(
            LaggedStart(ShowCreationThenDestruction, new_eddy),
            FadeIn(
                label,
                rate_func=there_and_back_with_pause,
            ),
            run_time=3
        )


class EddyReferenceWithLabel(EddyReference):
    CONFIG = {
        "label": "Eddy"
    }


class LargeEddyReference(EddyReference):
    CONFIG = {
        "radius": 1.5,
        "label": "Large eddy"
    }


class SmallEddyReference(EddyReference):
    CONFIG = {
        "radius": 0.25,
        "label": "Small eddy"
    }


class SomeTurbulenceEquations(PiCreatureScene):
    def construct(self):
        randy, morty = self.pi_creatures
        navier_stokes = NavierStokesEquations()
        line = Line(randy.get_right(), morty.get_left())
        navier_stokes.replace(line, dim_to_match=0)
        navier_stokes.scale(1.2)

        distribution = TexMobject(
            "E(k)=\\alpha \\epsilon^{2/3}_d k^{-5/3}",
            tex_to_color_map={
                "k": GREEN,
                "-5/3": YELLOW,
                "\\epsilon": BLUE,
                "_d": BLUE,
            }
        )
        distribution.next_to(morty, UL)
        brace = Brace(distribution, DOWN, buff=SMALL_BUFF)
        brace_words = brace.get_text("Explained soon...")
        brace_group = VGroup(brace, brace_words)

        self.play(
            Write(navier_stokes),
            randy.change, "confused", navier_stokes,
            morty.change, "confused", navier_stokes,
        )
        self.wait(3)
        self.play(
            morty.change, "raise_right_hand", distribution,
            randy.look_at, distribution,
            FadeInFromDown(distribution),
            navier_stokes.fade, 0.5,
        )
        self.play(GrowFromCenter(brace_group))
        self.play(randy.change, "pondering", distribution)
        self.wait(3)
        dist_group = VGroup(distribution, brace_group)
        self.play(
            LaggedStart(FadeOut, VGroup(randy, morty, navier_stokes)),
            dist_group.scale, 1.5,
            dist_group.center,
            dist_group.to_edge, UP,
        )
        self.wait()

    def create_pi_creatures(self):
        randy, morty = Randolph(), Mortimer()
        randy.to_corner(DL)
        morty.to_corner(DR)
        return (randy, morty)


class JokeRingEquation(Scene):
    def construct(self):
        items = VGroup(
            TextMobject("Container with a lip"),
            TextMobject("Fill with smoke (or fog)"),
            TextMobject("Hold awkwardly"),
        )
        line = Line(LEFT, RIGHT).set_width(items.get_width() + 1)
        items.add(line)
        items.add(TextMobject("Vortex ring"))
        items.arrange_submobjects(DOWN, buff=MED_LARGE_BUFF, aligned_edge=LEFT)
        line.shift(LEFT)
        plus = TexMobject("+")
        plus.next_to(line.get_left(), UR, SMALL_BUFF)
        line.add(plus)
        items.to_edge(RIGHT)

        point = 3.8 * LEFT + 0.2 * UP
        arrow1 = Arrow(
            items[0].get_left(), point + 0.8 * UP + 0.3 * RIGHT,
            use_rectangular_stem=False,
            path_arc=90 * DEGREES,
        )
        arrow1.pointwise_become_partial(arrow1, 0, 0.99)

        arrow2 = Arrow(
            items[1].get_left(), point,
        )
        arrows = VGroup(arrow1, arrow2)

        for i in 0, 1:
            self.play(
                FadeInFromDown(items[i]),
                ShowCreation(arrows[i])
            )
            self.wait()
        self.play(LaggedStart(FadeIn, items[2:]))
        self.wait()
        self.play(FadeOut(arrows))
        self.wait()


class VideoOnPhysicsGirlWrapper(Scene):
    def construct(self):
        rect = ScreenRectangle(height=6)
        title = TextMobject("Video on Physics Girl")
        title.scale(1.5)
        title.to_edge(UP)
        rect.next_to(title, DOWN)

        self.add(title)
        self.play(ShowCreation(rect))
        self.wait()


class LightBouncingOffFogParticle(Scene):
    def construct(self):
        words = TextMobject(
            "Light bouncing\\\\",
            "off fog particles"
        )
        arrow = Vector(UP + 0.5 * RIGHT)
        arrow.next_to(words, UP)
        arrow.set_color(WHITE)

        self.add(words)
        self.play(GrowArrow(arrow))
        self.wait()


class NightHawkInLightWrapper(Scene):
    def construct(self):
        title = TextMobject("NightHawkInLight")
        title.scale(1.5)
        title.to_edge(UP)
        rect = ScreenRectangle(height=6)
        rect.next_to(title, DOWN)
        self.add(title)
        self.play(ShowCreation(rect))
        self.wait()


class CarefulWithLasers(TeacherStudentsScene):
    def construct(self):
        morty = self.teacher
        randy = self.students[1]
        randy2 = self.students[2]
        # randy.change('hooray')
        laser = VGroup(
            Rectangle(
                height=0.1,
                width=0.3,
                fill_color=LIGHT_GREY,
                fill_opacity=1,
                stroke_color=DARK_GREY,
                stroke_width=1,
            ),
            Line(ORIGIN, 10 * RIGHT, color=GREEN_SCREEN)
        )
        laser.arrange_submobjects(RIGHT, buff=0)
        laser.rotate(45 * DEGREES)
        laser.shift(randy.get_corner(UR) - laser[0].get_center() + 0.1 * DR)

        laser.time = 0

        def update_laser(laser, dt):
            laser.time += dt
            laser.rotate(
                0.5 * dt * np.sin(laser.time),
                about_point=laser[0].get_center()
            )
        laser.add_updater(update_laser)

        self.play(LaggedStart(FadeInFromDown, self.pi_creatures, run_time=1))
        self.add(self.pi_creatures, laser)
        for pi in self.pi_creatures:
            pi.add_updater(lambda p: p.look_at(laser[1]))
        self.play(
            ShowCreation(laser),
            self.get_student_changes(
                "surprised", "hooray", "horrified",
                look_at_arg=laser
            )
        )
        self.teacher_says(
            "Careful with \\\\ the laser!",
            target_mode="angry"
        )
        self.wait(2.2)
        morty.save_state()
        randy2.save_state()
        self.play(
            morty.blink, randy2.blink,
            run_time=0.3
        )
        self.wait(2)
        self.play(
            morty.restore, randy2.restore,
            run_time=0.3
        )
        self.wait(2)


class SetAsideTurbulence(PiCreatureScene):
    def construct(self):
        self.pi_creature_says(
            "Forget vortex rings",
            target_mode="speaking"
        )
        self.wait()
        self.pi_creature_says(
            "look at that\\\\ turbulence!",
            target_mode="surprised"
        )
        self.wait()

    def create_pi_creature(self):
        morty = Mortimer()
        morty.to_corner(DR)
        return morty


class WavingRodLabel(Scene):
    def construct(self):
        words = TextMobject(
            "(Waving a glass rod \\\\ through the air)"
        )
        self.play(Write(words))
        self.wait()


class LongEddy(Scene):
    def construct(self):
        self.add(Eddy())
        self.wait(30)


class LongDoublePendulum(Scene):
    def construct(self):
        self.add(DoublePendulums())
        self.wait(30)


class LongDiffusion(Scene):
    def construct(self):
        self.add(Diffusion())
        self.wait(30)


class AskAboutTurbulence(TeacherStudentsScene):
    def construct(self):
        self.pi_creatures_ask()
        self.divide_by_qualitative_quantitative()
        self.three_qualitative_descriptors()
        self.rigorous_definition()

    def pi_creatures_ask(self):
        morty = self.teacher
        randy = self.students[1]
        morty.change("surprised")

        words = TextMobject("Wait,", "what", "exactly \\\\", "is turbulence?")
        question = TextMobject("What", "is turbulence?")
        question.to_edge(UP, buff=MED_SMALL_BUFF)
        h_line = Line(LEFT, RIGHT).set_width(FRAME_WIDTH - 1)
        h_line.next_to(question, DOWN, buff=MED_LARGE_BUFF)

        self.student_says(
            words,
            target_mode='raise_left_hand',
            added_anims=[morty.change, 'pondering']
        )
        self.change_student_modes(
            "erm", "raise_left_hand", "confused",
        )
        self.wait(3)
        self.play(
            morty.change, "raise_right_hand",
            FadeOut(randy.bubble),
            ReplacementTransform(VGroup(words[1], words[3]), question),
            FadeOut(VGroup(words[0], words[2])),
            self.get_student_changes(
                *3 * ["pondering"],
                look_at_arg=question
            )
        )
        self.play(
            ShowCreation(h_line),
            LaggedStart(
                FadeOutAndShiftDown, self.pi_creatures,
                run_time=1,
                lag_ratio=0.8
            )
        )
        self.wait()

        self.question = question
        self.h_line = h_line

    def divide_by_qualitative_quantitative(self):
        v_line = Line(
            self.h_line.get_center(),
            FRAME_HEIGHT * DOWN / 2,
        )
        words = VGroup(
            TextMobject("Features", color=YELLOW),
            TextMobject("Rigorous definition", color=BLUE),
        )
        words.next_to(self.h_line, DOWN)
        words[0].shift(FRAME_WIDTH * LEFT / 4)
        words[1].shift(FRAME_WIDTH * RIGHT / 4)
        self.play(
            ShowCreation(v_line),
            LaggedStart(FadeInFromDown, words)
        )
        self.wait()

    def three_qualitative_descriptors(self):
        words = VGroup(
            TextMobject("- Eddies"),
            TextMobject("- Chaos"),
            TextMobject("- Diffusion"),
        )
        words.arrange_submobjects(
            DOWN, buff=1.25,
            aligned_edge=LEFT
        )
        words.to_edge(LEFT)
        words.shift(MED_LARGE_BUFF * DOWN)

        # objects = VGroup(
        #     Eddy(),
        #     DoublePendulum(),
        #     Diffusion(),
        # )

        # for word, obj in zip(words, objects):
        for word in words:
            # obj.next_to(word, RIGHT)
            self.play(
                FadeInFromDown(word),
                # VFadeIn(obj)
            )
        self.wait(3)

    def rigorous_definition(self):
        randy = Randolph()
        randy.move_to(FRAME_WIDTH * RIGHT / 4)

        self.play(FadeIn(randy))
        self.play(randy.change, "shruggie")
        for x in range(2):
            self.play(Blink(randy))
            self.wait()


class BumpyPlaneRide(Scene):
    def construct(self):
        plane = SVGMobject(file_name="plane2")
        self.add(plane)

        total_time = 0
        while total_time < 10:
            point = 2 * np.append(np.random.random(2), 2) + DL
            point *= 0.2
            time = 0.2 * random.random()
            total_time += time
            arc = PI * random.random() - PI / 2
            self.play(
                plane.move_to, point,
                run_time=time,
                path_arc=arc
            )


class PureAirfoilFlowCopy(PureAirfoilFlow):
    def modify_vector_field(self, vector_field):
        PureAirfoilFlow.modify_vector_field(self, vector_field)
        vector_field.set_fill(opacity=0.1)
        vector_field.set_stroke(opacity=0.1)


class LaminarFlowLabel(Scene):
    def construct(self):
        words = TextMobject("Laminar flow")
        words.scale(1.5)
        words.to_edge(UP)
        subwords = TextMobject(
            "`Lamina', in Latin, means \\\\"
            "``a thin sheet of material''",
            tex_to_color_map={"Lamina": YELLOW},
            arg_separator="",
        )
        subwords.next_to(words, DOWN, MED_LARGE_BUFF)
        VGroup(words, subwords).set_background_stroke(width=4)
        self.play(Write(words))
        self.wait()
        self.play(FadeInFromDown(subwords))
        self.wait()


class HighCurlFieldBreakingLayers(Scene):
    CONFIG = {
        "flow_anim": VectorFieldSubmobjectFlow,
    }

    def construct(self):
        lines = VGroup(*[
            self.get_line()
            for x in range(20)
        ])
        lines.arrange_submobjects(DOWN, buff=MED_SMALL_BUFF)
        lines[0::2].set_color(BLUE)
        lines[1::2].set_color(RED)
        all_dots = VGroup(*it.chain(*lines))

        def func(p):
            vect = four_swirls_function(p)
            norm = get_norm(vect)
            if norm > 2:
                vect *= 4.0 / get_norm(vect)**2
            return vect

        self.add(lines)
        self.add(self.flow_anim(all_dots, func))
        self.wait(16)

    def get_line(self):
        line = VGroup(*[Dot() for x in range(100)])
        line.set_height(0.1)
        line.arrange_submobjects(RIGHT, buff=0)
        line.set_width(10)
        return line


class HighCurlFieldBreakingLayersLines(HighCurlFieldBreakingLayers):
    CONFIG = {
        "flow_anim": VectorFieldPointFlow
    }

    def get_line(self):
        line = Line(LEFT, RIGHT)
        line.insert_n_anchor_points(500)
        line.set_width(5)
        return line


class VorticitySynonyms(Scene):
    def construct(self):
        words = VGroup(
            TextMobject("High", "vorticity"),
            TexMobject(
                "\\text{a.k.a} \\,",
                "|\\nabla \\times \\vec{\\textbf{v}}| > 0"
            ),
            TextMobject("a.k.a", "high", "swirly-swirly", "factor"),
        )
        words[0].set_color_by_tex("vorticity", BLUE)
        words[1].set_color_by_tex("nabla", BLUE)
        words[2].set_color_by_tex("swirly", BLUE)
        words.arrange_submobjects(
            DOWN,
            aligned_edge=LEFT,
            buff=MED_LARGE_BUFF
        )

        for word in words:
            word.add_background_rectangle()
            self.play(FadeInFromDown(word))
            self.wait()


class VorticityDoesNotImplyTurbulence(TeacherStudentsScene):
    def construct(self):
        t_to_v = TextMobject(
            "Turbulence", "$\\Rightarrow$", "Vorticity",
        )
        v_to_t = TextMobject(
            "Vorticity", "$\\Rightarrow$", "Turbulence",
        )
        for words in t_to_v, v_to_t:
            words.move_to(self.hold_up_spot, DR)
            words.set_color_by_tex_to_color_map({
                "Vorticity": BLUE,
                "Turbulence": GREEN,
            })
        v_to_t.submobjects.reverse()
        cross = Cross(v_to_t[1])

        morty = self.teacher
        self.play(
            morty.change, "raise_right_hand",
            FadeInFromDown(t_to_v)
        )
        self.wait()
        self.play(t_to_v.shift, 2 * UP,)
        self.play(
            TransformFromCopy(t_to_v, v_to_t, path_arc=PI / 2),
            self.get_student_changes(
                "erm", "confused", "sassy",
                run_time=1
            ),
            ShowCreation(cross, run_time=2),
        )
        self.add(cross)
        self.wait(4)


class ShowNavierStokesEquations(Scene):
    def construct(self):
        pass
