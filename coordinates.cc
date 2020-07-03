#include <stack>
#include <utility>
#include <string>

#include "coordinates.h"
#include "common.h"
#include "fonts.h"

using namespace std;

namespace
{
    matrix_t translate_matrix(const matrix_t &m, float x, float y)
    {
        return matrix_t{m[0], m[1], m[2], m[3], x * m[0] + y * m[2] + m[4], x * m[1] + y * m[3] + m[5]};
    }

    matrix_t get_matrix(stack<pair<pdf_object_t, string>> &st)
    {
        float f = stof(pop(st).second);
        float e = stof(pop(st).second);
        float d = stof(pop(st).second);
        float c = stof(pop(st).second);
        float b = stof(pop(st).second);
        float a = stof(pop(st).second);
        return matrix_t{a, b, c, d, e, f};
    }
}

Coordinates::Coordinates(const matrix_t &CTM_arg):
    CTM(CTM_arg),
    Tm(IDENTITY_MATRIX),
    Tfs(TFS_DEFAULT),
    Th(TH_DEFAULT),
    Tc(TC_DEFAULT),
    Tw(TW_DEFAULT),
    TL(TL_DEFAULT),
    x(0),
    y(0)
{
}

matrix_t Coordinates::get_CTM() const
{
    return CTM;
}

void Coordinates::T_quote()
{
    T_star();
}

void Coordinates::T_star()
{
    Td(0, -TL);
}

void Coordinates::Td(float x_a, float y_a)
{
    Tm = matrix_t{Tm[0], Tm[1], Tm[2], Tm[3], x_a * Tm[0] + y_a * Tm[2] + Tm[4], x_a * Tm[1] + y_a * Tm[3] + Tm[5]};
    x = 0;
    y = 0;
}

void Coordinates::set_default()
{
    Tm = IDENTITY_MATRIX;
    x = 0;
    y = 0;
}

pair<float, float> apply_matrix_pt(const matrix_t &m, float x, float y)
{
    return make_pair(m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]);
}

text_chunk_t Coordinates::adjust_coordinates(string &&s, size_t len, float width, float Tj, const Fonts &fonts)
{
    if (Tj != 0)
    {
        x -= Tj * Tfs * Th * 0.001;
        x += Tc * Th;
    }
    float ty = fonts.get_descent() * Tfs + fonts.get_rise() * Tfs;
    float adv = width * Tfs * Th;
    const matrix_t T_start = translate_matrix(Tm * CTM, x, y);
    if (len > 1) x += Tc * Th * (len - 1);
    for (char c : s)
    {
        if (c == ' ') x += Tw * Th;
    }
    const matrix_t T_end = translate_matrix(Tm * CTM, x, y);
    const pair<float, float> start_coordinates = apply_matrix_pt(T_start, 0, ty);
    const pair<float, float> end_coordinates = apply_matrix_pt(T_end, adv, ty + fonts.get_height() * Tfs);
    float x0 = min(start_coordinates.first, end_coordinates.first);
    float x1 = max(start_coordinates.first, end_coordinates.first);
    float y0 = min(start_coordinates.second, end_coordinates.second);
    float y1 = max(start_coordinates.second, end_coordinates.second);
    x += adv;
//    cout << s << " (" << x0 << ", " << y0 << ")(" << x1 << ", " << y1 << ") " << width << endl;
    return text_chunk_t(std::move(s), coordinates_t(x0, y0, x1, y1));
}

void Coordinates::ctm_work(const string &token, stack<pair<pdf_object_t, string>> &st)
{
    if (token == "cm") CTM = get_matrix(st) * CTM;
    else if (token == "q") CTMs.push(CTM);
    else if (token == "Q"  && !CTMs.empty()) CTM = pop(CTMs);
}

void Coordinates::set_Tz(const string &token, stack<pair<pdf_object_t, string>> &st)
{
    //Th in percentages
    Th = stof(pop(st).second) / 100;
}

void Coordinates::set_TL(const string &token, stack<pair<pdf_object_t, string>> &st)
{
    TL = stof(pop(st).second);
}

void Coordinates::set_Tc(const string &token, stack<pair<pdf_object_t, string>> &st)
{
    Tc = stof(pop(st).second);
}

void Coordinates::set_Tw(const string &token, stack<pair<pdf_object_t, string>> &st)
{
    Tw = stof(pop(st).second);
}

void Coordinates::set_Td(const string &token, stack<pair<pdf_object_t, string>> &st)
{
        float y = stof(pop(st).second);
        float x = stof(pop(st).second);
        Td(x, y);
}

void Coordinates::set_TD(const string &token, stack<pair<pdf_object_t, string>> &st)
{
    float y = stof(pop(st).second);
    float x = stof(pop(st).second);
    Td(x, y);
    TL = -y;
}

void Coordinates::set_Tm(const string &token, stack<pair<pdf_object_t, string>> &st)
{
    Tm = get_matrix(st);
    x = 0;
    y = 0;
}

void Coordinates::set_T_star(const string &token, stack<pair<pdf_object_t, string>> &st)
{
    T_star();
}

void Coordinates::set_coordinates(const string &token, stack<pair<pdf_object_t, string>> &st)
{
    if (token == "Tz")
    {
        //Th in percentages
        Th = stof(pop(st).second) / 100;
    }
    else if (token == "'")
    {
        T_quote();
    }
    else if (token == "\"")
    {
        Tc = stof(pop(st).second);
        Tw = stof(pop(st).second);
        T_quote();
    }
    else if (token == "TL")
    {
        TL = stof(pop(st).second);
    }
    else if (token == "T*")
    {
        T_star();
    }
    else if (token == "Tc")
    {
        Tc = stof(pop(st).second);
    }
    else if (token == "Tw")
    {
        Tw = stof(pop(st).second);
    }
    else if (token == "Td")
    {
        float y = stof(pop(st).second);
        float x = stof(pop(st).second);
        Td(x, y);
    }
    else if (token == "TD")
    {
        float y = stof(pop(st).second);
        float x = stof(pop(st).second);
        Td(x, y);
        TL = -y;
    }
    else if (token == "Tm")
    {
        Tm = get_matrix(st);
        x = 0;
        y = 0;
    }
    else if (token == "Tf")
    {
        Tfs = stof(pop(st).second);
    }
    else
    {
        throw pdf_error(FUNC_STRING + "unknown token:" + token);
    }
}
