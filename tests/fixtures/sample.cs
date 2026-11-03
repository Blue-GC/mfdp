using System;
using System.Collections.Generic;

namespace Demo
{
    public interface IShape
    {
        double Area();
    }

    public class Circle : IShape
    {
        private readonly double radius;
        public const double Pi = 3.14159;

        public Circle(double radius)
        {
            this.radius = radius;
        }

        /// <summary>Area of the circle.</summary>
        public double Area()
        {
            return Pi * radius * radius;
        }

        public static double Total(IEnumerable<Circle> circles)
        {
            double sum = 0;
            foreach (var c in circles)
            {
                sum += c.Area();
            }
            return sum;
        }
    }
}
