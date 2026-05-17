using UnityEngine;

public class PlayerRotateWithInertia : MonoBehaviour
{
    public float Velocity { get; private set; }

    void Update()
    {
        Velocity = 4f; // Current velocity of rotation (measured in deg/s)
        transform.Rotate(Vector3.up, Velocity * Time.deltaTime);
    }
}