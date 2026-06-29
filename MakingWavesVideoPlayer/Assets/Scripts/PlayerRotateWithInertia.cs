using UnityEngine;
using UnityEngine.InputSystem;

public class PlayerRotateWithInertia : MonoBehaviour
{
    private record MovementKeys(Key Key, float Velocity)
    {
        public float Velocity { get; } = Velocity;
        public Key Key { get; } = Key;
    }

    private const float Vel1 = 10f;
    private const float Vel2 = 2 * Vel1;
    private const float Vel3 = 2 * Vel2;
    private const float Vel4 = 2 * Vel3;

    private static readonly MovementKeys[] Keys =
    {
        new(Key.A, -Vel4),
        new(Key.S, -Vel3),
        new(Key.D, -Vel2),
        new(Key.F, -Vel1),
        new(Key.H, Vel1),
        new(Key.J, Vel2),
        new(Key.K, Vel3),
        new(Key.L, Vel4),
    };
    
    public float Velocity { get; private set; }
    
    void Update()
    {
        float newVelocity = 0;

        foreach (MovementKeys mKey in Keys)
        {
            if (Keyboard.current[mKey.Key].isPressed)
            {
                newVelocity = mKey.Velocity;
                break;
            }
        }

        Velocity = Mathf.Lerp(Velocity, newVelocity, 10 * Time.deltaTime);
        if (Mathf.Abs(Velocity) < 0.1)
        {
            Velocity = 0;
        }

        transform.Rotate(Vector3.up, Velocity * Time.deltaTime);
    }
}